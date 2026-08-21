#include "mcd/core/json.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/wait.h>

#ifndef MCD_CLI_PATH
#error "MCD_CLI_PATH must be defined by the build (see tests/CMakeLists.txt)"
#endif

namespace {

struct CliRun {
    std::string stdout_text;
    int exit_code;
};

// Runs the built mcd_cli binary as a subprocess: writes `input_json` to a temp file, feeds
// it in via shell redirection, and captures stdout -- an integration test of the real
// process boundary (argv/stdin/stdout/exit code), not a unit test of handle_request()
// in-process. See docs/design/06-cli-bindings-reporting.md sec.7.
CliRun run_cli(const std::string& input_json) {
    const std::string tmp_path =
        std::string(::testing::TempDir()) + "mcd_cli_test_input.json";
    {
        std::ofstream f(tmp_path);
        f << input_json;
    }
    const std::string command =
        std::string("\"") + MCD_CLI_PATH + "\" < \"" + tmp_path + "\"";
    FILE* pipe = popen(command.c_str(), "r");
    if (pipe == nullptr) {
        ADD_FAILURE() << "failed to launch mcd_cli";
        return {"", -1};
    }
    std::ostringstream out;
    std::array<char, 4096> buf{};
    std::size_t n = 0;
    while ((n = std::fread(buf.data(), 1, buf.size(), pipe)) > 0) {
        out.write(buf.data(), static_cast<std::streamsize>(n));
    }
    const int status = pclose(pipe);
    const int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return {out.str(), exit_code};
}

} // namespace

TEST(McdCli, EuropeanCallSucceedsWithFullSchema) {
    const auto run = run_cli(
        R"({"product":"european","spot":100,"strike":100,"rate":0.05,"carry_yield":0.0,)"
        R"("vol":0.2,"time":1.0,"type":"call","path_count":200000,"seed":42})");
    ASSERT_EQ(run.exit_code, 0) << run.stdout_text;
    const auto obj = mcd::json::parse_object(run.stdout_text);
    for (const char* field : {"price", "standard_error", "ci_95_low", "ci_95_high", "path_count",
                               "seed", "elapsed_seconds", "paths_per_second"}) {
        EXPECT_NE(mcd::json::find(obj, field), nullptr) << "missing field: " << field;
    }
    const double price = mcd::json::find(obj, "price")->as_number();
    EXPECT_NEAR(price, 10.45, 0.5);
}

TEST(McdCli, EuropeanGreeksReturnsAllFiveGreeks) {
    const auto run = run_cli(
        R"({"product":"european","request":"greeks","spot":100,"strike":100,"rate":0.05,)"
        R"("carry_yield":0.0,"vol":0.2,"time":1.0,"type":"call","path_count":200000,)"
        R"("seed":42})");
    ASSERT_EQ(run.exit_code, 0) << run.stdout_text;
    const auto obj = mcd::json::parse_object(run.stdout_text);
    for (const char* field : {"delta", "gamma", "vega", "theta", "rho"}) {
        EXPECT_NE(mcd::json::find(obj, field), nullptr) << "missing field: " << field;
    }
}

TEST(McdCli, EuropeanLrGreeksReturnsAllFiveGreeksWithStandardErrors) {
    const auto run = run_cli(
        R"({"product":"european","request":"lr_greeks","spot":100,"strike":100,"rate":0.05,)"
        R"("carry_yield":0.0,"vol":0.2,"time":1.0,"type":"call","path_count":200000,)"
        R"("seed":42})");
    ASSERT_EQ(run.exit_code, 0) << run.stdout_text;
    const auto obj = mcd::json::parse_object(run.stdout_text);
    for (const char* field : {"delta", "delta_se", "gamma", "gamma_se", "vega", "vega_se",
                               "theta", "theta_se", "rho", "rho_se"}) {
        EXPECT_NE(mcd::json::find(obj, field), nullptr) << "missing field: " << field;
    }
}

TEST(McdCli, DigitalLrGreeksReturnsAllFiveGreeks) {
    const auto run = run_cli(
        R"({"product":"digital","request":"lr_greeks","spot":100,"strike":100,"rate":0.05,)"
        R"("carry_yield":0.0,"vol":0.2,"time":1.0,"type":"call",)"
        R"("digital_style":"cash_or_nothing","path_count":200000,"seed":7})");
    ASSERT_EQ(run.exit_code, 0) << run.stdout_text;
    const auto obj = mcd::json::parse_object(run.stdout_text);
    for (const char* field : {"delta", "gamma", "vega", "theta", "rho"}) {
        EXPECT_NE(mcd::json::find(obj, field), nullptr) << "missing field: " << field;
    }
}

TEST(McdCli, BarrierLrGreeksOmitsThetaRatherThanReportingAMisleadingZero) {
    const auto run = run_cli(
        R"({"product":"barrier","request":"lr_greeks","spot":100,"strike":100,"barrier":120,)"
        R"("rate":0.05,"carry_yield":0.0,"vol":0.2,"time":1.0,"type":"call","direction":"up",)"
        R"("knock":"out","monitoring_points":50,"path_count":50000,"seed":11})");
    ASSERT_EQ(run.exit_code, 0) << run.stdout_text;
    const auto obj = mcd::json::parse_object(run.stdout_text);
    for (const char* field : {"delta", "gamma", "vega", "rho"}) {
        EXPECT_NE(mcd::json::find(obj, field), nullptr) << "missing field: " << field;
    }
    EXPECT_EQ(mcd::json::find(obj, "theta"), nullptr)
        << "barrier LR Greeks should omit theta entirely, not report a misleading zero";
}

TEST(McdCli, EuropeanPathwiseGreeksReturnsThreeGreeksNoGammaOrTheta) {
    const auto run = run_cli(
        R"({"product":"european","request":"pathwise_greeks","spot":100,"strike":100,)"
        R"("rate":0.05,"carry_yield":0.0,"vol":0.2,"time":1.0,"type":"call",)"
        R"("path_count":200000,"seed":42})");
    ASSERT_EQ(run.exit_code, 0) << run.stdout_text;
    const auto obj = mcd::json::parse_object(run.stdout_text);
    for (const char* field : {"delta", "delta_se", "vega", "vega_se", "rho", "rho_se"}) {
        EXPECT_NE(mcd::json::find(obj, field), nullptr) << "missing field: " << field;
    }
    for (const char* field : {"gamma", "theta"}) {
        EXPECT_EQ(mcd::json::find(obj, field), nullptr)
            << "pathwise Greeks should never report " << field
            << " -- structurally undefined, not just unavailable for this product";
    }
}

TEST(McdCli, AsianPathwiseGreeksReturnsThreeGreeks) {
    const auto run = run_cli(
        R"({"product":"asian","request":"pathwise_greeks","spot":100,"strike":100,)"
        R"("rate":0.05,"carry_yield":0.0,"vol":0.2,"time":1.0,"type":"call",)"
        R"("strike_style":"fixed","average_style":"arithmetic","monitoring_points":12,)"
        R"("path_count":50000,"seed":42})");
    ASSERT_EQ(run.exit_code, 0) << run.stdout_text;
    const auto obj = mcd::json::parse_object(run.stdout_text);
    for (const char* field : {"delta", "vega", "rho"}) {
        EXPECT_NE(mcd::json::find(obj, field), nullptr) << "missing field: " << field;
    }
}

TEST(McdCli, EuropeanQmcSobolReturnsPriceWithoutStandardError) {
    const auto run = run_cli(
        R"({"product":"european","request":"qmc_sobol","spot":100,"strike":100,)"
        R"("rate":0.05,"carry_yield":0.0,"vol":0.2,"time":1.0,"type":"call",)"
        R"("path_count":20000})");
    ASSERT_EQ(run.exit_code, 0) << run.stdout_text;
    const auto obj = mcd::json::parse_object(run.stdout_text);
    EXPECT_NE(mcd::json::find(obj, "price"), nullptr);
    EXPECT_NE(mcd::json::find(obj, "note"), nullptr);
    EXPECT_EQ(mcd::json::find(obj, "standard_error"), nullptr)
        << "Sobol QMC is deterministic -- no standard_error should be reported";
    EXPECT_EQ(mcd::json::find(obj, "seed"), nullptr) << "qmc_sobol takes no seed";
}

TEST(McdCli, AsianQmcSobolReturnsPrice) {
    const auto run = run_cli(
        R"({"product":"asian","request":"qmc_sobol","spot":100,"strike":100,)"
        R"("rate":0.05,"carry_yield":0.0,"vol":0.25,"time":1.0,"type":"call",)"
        R"("strike_style":"fixed","average_style":"arithmetic","monitoring_points":7,)"
        R"("path_count":10000})");
    ASSERT_EQ(run.exit_code, 0) << run.stdout_text;
    const auto obj = mcd::json::parse_object(run.stdout_text);
    EXPECT_NE(mcd::json::find(obj, "price"), nullptr);
}

TEST(McdCli, AsianQmcSobolRejectsMonitoringPointsAboveDimensionCap) {
    const auto run = run_cli(
        R"({"product":"asian","request":"qmc_sobol","spot":100,"strike":100,)"
        R"("rate":0.05,"carry_yield":0.0,"vol":0.25,"time":1.0,"type":"call",)"
        R"("strike_style":"fixed","average_style":"arithmetic","monitoring_points":9,)"
        R"("path_count":10000})");
    ASSERT_NE(run.exit_code, 0);
    const auto obj = mcd::json::parse_object(run.stdout_text);
    EXPECT_NE(mcd::json::find(obj, "error"), nullptr);
}

TEST(McdCli, ForwardReturnsPriceWithoutConfidenceInterval) {
    const auto run =
        run_cli(R"({"product":"forward","spot":100,"rate":0.05,"carry_yield":0.02,"time":1.0})");
    ASSERT_EQ(run.exit_code, 0) << run.stdout_text;
    const auto obj = mcd::json::parse_object(run.stdout_text);
    EXPECT_NE(mcd::json::find(obj, "price"), nullptr);
    EXPECT_EQ(mcd::json::find(obj, "standard_error"), nullptr);
    const double price = mcd::json::find(obj, "price")->as_number();
    EXPECT_NEAR(price, 100.0 * std::exp((0.05 - 0.02) * 1.0), 1e-6);
}

TEST(McdCli, BinomialAmericanReturnsPriceWithoutConfidenceInterval) {
    const auto run = run_cli(
        R"({"product":"binomial_american","spot":100,"strike":100,"rate":0.05,)"
        R"("carry_yield":0.0,"vol":0.2,"time":1.0,"type":"put","steps":200})");
    ASSERT_EQ(run.exit_code, 0) << run.stdout_text;
    const auto obj = mcd::json::parse_object(run.stdout_text);
    EXPECT_NE(mcd::json::find(obj, "price"), nullptr);
    EXPECT_EQ(mcd::json::find(obj, "standard_error"), nullptr);
}

TEST(McdCli, AmericanLsmReturnsFullSchema) {
    const auto run = run_cli(
        R"({"product":"american","spot":100,"strike":100,"rate":0.05,"carry_yield":0.0,)"
        R"("vol":0.25,"time":1.0,"type":"put","monitoring_points":10,"path_count":20000,)"
        R"("seed":7})");
    ASSERT_EQ(run.exit_code, 0) << run.stdout_text;
    const auto obj = mcd::json::parse_object(run.stdout_text);
    for (const char* field : {"price", "standard_error", "ci_95_low", "ci_95_high"}) {
        EXPECT_NE(mcd::json::find(obj, field), nullptr) << "missing field: " << field;
    }
}

TEST(McdCli, DigitalAsianBarrierLookbackAllReturnFullSchema) {
    const std::array<std::string, 4> requests = {
        R"({"product":"digital","spot":100,"strike":100,"rate":0.05,"carry_yield":0.0,)"
        R"("vol":0.2,"time":1.0,"type":"call","digital_style":"cash_or_nothing",)"
        R"("path_count":20000,"seed":1})",
        R"({"product":"asian","spot":100,"strike":100,"rate":0.05,"carry_yield":0.0,)"
        R"("vol":0.2,"time":1.0,"type":"call","strike_style":"fixed",)"
        R"("average_style":"geometric","monitoring_points":12,"path_count":20000,"seed":1})",
        R"({"product":"barrier","spot":100,"strike":100,"barrier":120,"rate":0.05,)"
        R"("carry_yield":0.0,"vol":0.2,"time":1.0,"type":"call","direction":"up",)"
        R"("knock":"out","monitoring_points":20,"path_count":20000,"seed":1})",
        R"({"product":"lookback","spot":100,"rate":0.05,"carry_yield":0.0,"vol":0.2,)"
        R"("time":1.0,"type":"call","style":"floating","monitoring_points":20,)"
        R"("path_count":20000,"seed":1})",
    };
    for (const auto& req : requests) {
        const auto run = run_cli(req);
        ASSERT_EQ(run.exit_code, 0) << req << " -> " << run.stdout_text;
        const auto obj = mcd::json::parse_object(run.stdout_text);
        EXPECT_NE(mcd::json::find(obj, "price"), nullptr) << req;
        EXPECT_NE(mcd::json::find(obj, "standard_error"), nullptr) << req;
        EXPECT_NE(mcd::json::find(obj, "ci_95_low"), nullptr) << req;
        EXPECT_NE(mcd::json::find(obj, "ci_95_high"), nullptr) << req;
    }
}

TEST(McdCli, MalformedJsonProducesCleanErrorNotCrash) {
    const auto run = run_cli("this is not json");
    EXPECT_NE(run.exit_code, 0);
    const auto obj = mcd::json::parse_object(run.stdout_text);
    EXPECT_NE(mcd::json::find(obj, "error"), nullptr);
}

TEST(McdCli, MissingRequiredFieldProducesCleanError) {
    const auto run = run_cli(R"({"product":"european"})");
    EXPECT_NE(run.exit_code, 0);
    const auto obj = mcd::json::parse_object(run.stdout_text);
    EXPECT_NE(mcd::json::find(obj, "error"), nullptr);
}

TEST(McdCli, UnknownProductProducesCleanError) {
    const auto run = run_cli(R"({"product":"not_a_real_product"})");
    EXPECT_NE(run.exit_code, 0);
    const auto obj = mcd::json::parse_object(run.stdout_text);
    EXPECT_NE(mcd::json::find(obj, "error"), nullptr);
}

TEST(McdCli, SeedAboveExactDoublePrecisionIsRejected) {
    const auto run = run_cli(
        R"({"product":"european","spot":100,"strike":100,"rate":0.05,"carry_yield":0.0,)"
        R"("vol":0.2,"time":1.0,"type":"call","path_count":1000,"seed":1e20})");
    EXPECT_NE(run.exit_code, 0);
    const auto obj = mcd::json::parse_object(run.stdout_text);
    EXPECT_NE(mcd::json::find(obj, "error"), nullptr);
}
