import * as path from "path";
import * as cdk from "aws-cdk-lib";
import { Construct } from "constructs";
import * as lambda from "aws-cdk-lib/aws-lambda";
import * as apigwv2 from "aws-cdk-lib/aws-apigatewayv2";
import { HttpLambdaIntegration } from "aws-cdk-lib/aws-apigatewayv2-integrations";
import * as s3 from "aws-cdk-lib/aws-s3";
import * as cloudfront from "aws-cdk-lib/aws-cloudfront";
import * as origins from "aws-cdk-lib/aws-cloudfront-origins";
import * as budgets from "aws-cdk-lib/aws-budgets";
import * as s3deploy from "aws-cdk-lib/aws-s3-deployment";

/**
 * The mcd demo stack (CLAUDE.md sec.6 Phase 7). Cost guardrails below are all mandatory,
 * per docs/design/07-aws-demo.md sec.3.2/sec.5 -- not optional hardening, the reason this
 * whole architecture is safe to leave deployed indefinitely at zero traffic:
 *  - Lambda: ARM64/Graviton, scale-to-zero, 30s timeout, path_count capped in the handler.
 *  - API Gateway HTTP API: throttled (burst 10, rate 5 req/s).
 *  - No authentication, no database, no VPC, no NAT Gateway.
 *  - AWS Budgets alarm at a low monthly threshold.
 *  - CloudFront caching in front of both the static frontend and the API's GET routes.
 */
export class McdDemoStack extends cdk.Stack {
  constructor(scope: Construct, id: string, props?: cdk.StackProps) {
    super(scope, id, props);

    // ---- Backend: Lambda container image (ARM64), built from infra/lambda/ -------------
    const repoRoot = path.join(__dirname, "..", "..");
    const priceFn = new lambda.DockerImageFunction(this, "PriceFunction", {
      code: lambda.DockerImageCode.fromImageAsset(repoRoot, {
        file: "infra/lambda/Dockerfile",
        platform: cdk.aws_ecr_assets.Platform.LINUX_ARM64,
      }),
      architecture: lambda.Architecture.ARM_64,
      timeout: cdk.Duration.seconds(30),
      // Starting point, not a guess left unmeasured: CLAUDE.md sec.6 Phase 7 requires
      // memory "tuned by measurement" -- this value is revisited once real cold/warm
      // invocation timings exist post-deploy (docs/design/07-aws-demo.md sec.7 item 4),
      // and this comment is updated with the real numbers, not silently left as a guess.
      memorySize: 1024,
      description: "mcd demo pricing API (CLAUDE.md sec.6 Phase 7)",
    });

    // ---- API Gateway HTTP API, throttled, CORS open (no auth on this demo) ------------
    const httpApi = new apigwv2.HttpApi(this, "Api", {
      description: "mcd demo pricing API",
      corsPreflight: {
        allowOrigins: ["*"],
        allowMethods: [apigwv2.CorsHttpMethod.GET, apigwv2.CorsHttpMethod.POST],
        allowHeaders: ["Content-Type"],
      },
      createDefaultStage: false,
    });

    const integration = new HttpLambdaIntegration("PriceIntegration", priceFn);
    httpApi.addRoutes({ path: "/price", methods: [apigwv2.HttpMethod.POST], integration });
    httpApi.addRoutes({
      path: "/cfa-invariants",
      methods: [apigwv2.HttpMethod.GET],
      integration,
    });

    // Burst 10 / rate 5 req/s -- CLAUDE.md sec.6 Phase 7's explicit throttling requirement.
    new apigwv2.HttpStage(this, "DefaultStage", {
      httpApi,
      stageName: "$default",
      autoDeploy: true,
      throttle: { rateLimit: 5, burstLimit: 10 },
    });

    // ---- Frontend: private S3 + CloudFront (Origin Access Control, no public bucket) --
    const siteBucket = new s3.Bucket(this, "SiteBucket", {
      blockPublicAccess: s3.BlockPublicAccess.BLOCK_ALL,
      // Demo/throwaway infrastructure by design (CLAUDE.md sec.6 Phase 7: scale-to-zero,
      // no database, meant to be cheap to run and cheap to tear down) -- destroying the
      // bucket's contents on stack deletion is the intended behavior here, not an
      // accident. Never apply this pattern to a bucket holding real user data.
      removalPolicy: cdk.RemovalPolicy.DESTROY,
      autoDeleteObjects: true,
      encryption: s3.BucketEncryption.S3_MANAGED,
    });

    const apiOrigin = new origins.HttpOrigin(`${httpApi.httpApiId}.execute-api.${this.region}.${this.urlSuffix}`);

    const distribution = new cloudfront.Distribution(this, "Distribution", {
      defaultRootObject: "index.html",
      defaultBehavior: {
        origin: origins.S3BucketOrigin.withOriginAccessControl(siteBucket),
        viewerProtocolPolicy: cloudfront.ViewerProtocolPolicy.REDIRECT_TO_HTTPS,
        cachePolicy: cloudfront.CachePolicy.CACHING_OPTIMIZED,
      },
      additionalBehaviors: {
        // CloudFront only ever caches GET/HEAD by default regardless of cache policy --
        // POST /price passes straight through uncached; GET /cfa-invariants gets a short
        // TTL, per CLAUDE.md's "CloudFront caching on all GET responses" (sec.6 Phase 7).
        "/price": {
          origin: apiOrigin,
          viewerProtocolPolicy: cloudfront.ViewerProtocolPolicy.REDIRECT_TO_HTTPS,
          allowedMethods: cloudfront.AllowedMethods.ALLOW_ALL,
          cachePolicy: cloudfront.CachePolicy.CACHING_DISABLED,
          originRequestPolicy: cloudfront.OriginRequestPolicy.ALL_VIEWER_EXCEPT_HOST_HEADER,
        },
        "/cfa-invariants": {
          origin: apiOrigin,
          viewerProtocolPolicy: cloudfront.ViewerProtocolPolicy.REDIRECT_TO_HTTPS,
          allowedMethods: cloudfront.AllowedMethods.ALLOW_GET_HEAD,
          cachePolicy: new cloudfront.CachePolicy(this, "CfaInvariantsCachePolicy", {
            defaultTtl: cdk.Duration.minutes(5),
            minTtl: cdk.Duration.seconds(0),
            maxTtl: cdk.Duration.hours(1),
          }),
        },
      },
      priceClass: cloudfront.PriceClass.PRICE_CLASS_100,
    });

    new s3deploy.BucketDeployment(this, "DeploySite", {
      sources: [s3deploy.Source.asset(path.join(repoRoot, "web", "dist"))],
      destinationBucket: siteBucket,
      distribution,
      distributionPaths: ["/*"],
    });

    // ---- Cost guardrail: AWS Budgets alarm, CLAUDE.md sec.6 Phase 7 mandatory ----------
    // Alert email deliberately not hardcoded into committed source (avoids baking personal
    // contact info into version-controlled infra code) -- pass at synth/deploy time:
    //   cdk deploy -c alertEmail=you@example.com
    // Without it, the budget is still created (visible/trackable in the console) but with
    // no notification subscriber; a warning is emitted at synth time either way.
    const alertEmail = this.node.tryGetContext("alertEmail") as string | undefined;
    const monthlyLimitUsd = Number(this.node.tryGetContext("budgetLimitUsd") ?? 10);

    new budgets.CfnBudget(this, "MonthlyBudget", {
      budget: {
        budgetType: "COST",
        timeUnit: "MONTHLY",
        budgetLimit: { amount: monthlyLimitUsd, unit: "USD" },
      },
      notificationsWithSubscribers: alertEmail
        ? [
            {
              notification: {
                notificationType: "ACTUAL",
                comparisonOperator: "GREATER_THAN",
                threshold: 80,
                thresholdType: "PERCENTAGE",
              },
              subscribers: [{ subscriptionType: "EMAIL", address: alertEmail }],
            },
          ]
        : undefined,
    });
    if (!alertEmail) {
      cdk.Annotations.of(this).addWarning(
        "No -c alertEmail=... provided: the AWS Budgets alarm was created but has no " +
          "notification subscriber. Redeploy with -c alertEmail=you@example.com to get " +
          "notified.",
      );
    }

    // ---- Outputs ------------------------------------------------------------------------
    new cdk.CfnOutput(this, "ApiUrl", { value: httpApi.apiEndpoint });
    new cdk.CfnOutput(this, "CloudFrontUrl", { value: `https://${distribution.domainName}` });
  }
}
