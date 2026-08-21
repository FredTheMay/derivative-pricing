#!/usr/bin/env node
import * as cdk from "aws-cdk-lib";
import { McdDemoStack } from "../lib/mcd-stack";

const app = new cdk.App();

new McdDemoStack(app, "McdDemoStack", {
  env: {
    account: process.env.CDK_DEFAULT_ACCOUNT,
    region: process.env.CDK_DEFAULT_REGION,
  },
  description: "mcd Monte Carlo derivatives pricing engine -- AWS demo (CLAUDE.md sec.6 Phase 7)",
});
