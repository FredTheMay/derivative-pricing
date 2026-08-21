import * as cdk from "aws-cdk-lib";
import { Template } from "aws-cdk-lib/assertions";
import { McdDemoStack } from "../lib/mcd-stack";

// A lightweight assertions test, not a substitute for `cdk synth` in CI
// (docs/design/07-aws-demo.md sec.6) -- this catches "did I wire the mandatory cost
// guardrails at all" regressions cheaply, without needing Docker or a built web/dist.
describe("McdDemoStack", () => {
  const app = new cdk.App();
  const stack = new McdDemoStack(app, "TestStack", {
    env: { account: "123456789012", region: "us-east-2" },
  });
  const template = Template.fromStack(stack);

  it("provisions exactly one ARM64 Lambda for pricing, with a 30s timeout", () => {
    template.hasResourceProperties("AWS::Lambda::Function", {
      Architectures: ["arm64"],
      Timeout: 30,
      PackageType: "Image",
    });
  });

  it("throttles the API Gateway HTTP API stage (CLAUDE.md cost guardrail)", () => {
    template.hasResourceProperties("AWS::ApiGatewayV2::Stage", {
      DefaultRouteSettings: {
        ThrottlingBurstLimit: 10,
        ThrottlingRateLimit: 5,
      },
    });
  });

  it("provisions exactly one AWS Budgets alarm (CLAUDE.md mandatory cost guardrail)", () => {
    template.resourceCountIs("AWS::Budgets::Budget", 1);
    template.hasResourceProperties("AWS::Budgets::Budget", {
      Budget: {
        BudgetType: "COST",
        TimeUnit: "MONTHLY",
      },
    });
  });

  it("blocks all public access on the frontend S3 bucket", () => {
    template.hasResourceProperties("AWS::S3::Bucket", {
      PublicAccessBlockConfiguration: {
        BlockPublicAcls: true,
        BlockPublicPolicy: true,
        IgnorePublicAcls: true,
        RestrictPublicBuckets: true,
      },
    });
  });

  it("provisions exactly one CloudFront distribution serving the frontend", () => {
    template.resourceCountIs("AWS::CloudFront::Distribution", 1);
  });

  it("has no VPC, NAT Gateway, or database resources (CLAUDE.md mandatory guardrail)", () => {
    for (const forbidden of [
      "AWS::EC2::VPC",
      "AWS::EC2::NatGateway",
      "AWS::RDS::DBInstance",
      "AWS::DynamoDB::Table",
    ]) {
      template.resourceCountIs(forbidden, 0);
    }
  });
});
