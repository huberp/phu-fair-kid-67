#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Row {
    float inputDbfs{};
    float outputDbfs{};
    float grDb{};
};

std::vector<Row> loadRows(const std::filesystem::path& path)
{
    std::ifstream in(path);
    if (!in.is_open()) {
        return {};
    }

    std::vector<Row> rows;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        if (line.find("input_dbfs") != std::string::npos) {
            continue;
        }

        std::stringstream ss(line);
        std::string cell;
        std::vector<std::string> cols;
        while (std::getline(ss, cell, ',')) {
            cols.push_back(cell);
        }
        if (cols.size() < 3) {
            continue;
        }

        int offset = 0;
        if (cols[0] == "up" || cols[0] == "down") {
            offset = 1;
        }
        if (cols.size() < static_cast<std::size_t>(offset + 3)) {
            continue;
        }

        try {
            rows.push_back(Row{
                std::stof(cols[offset + 0]),
                std::stof(cols[offset + 1]),
                std::stof(cols[offset + 2]),
            });
        } catch (...) {
        }
    }

    return rows;
}

const Row& nearestRow(const std::vector<Row>& rows, const float inputDbfs)
{
    return *std::min_element(rows.begin(), rows.end(), [=](const Row& a, const Row& b) {
        return std::abs(a.inputDbfs - inputDbfs) < std::abs(b.inputDbfs - inputDbfs);
    });
}

} // namespace

TEST_CASE("TransferFamily: reference CSV files are present and non-empty", "[transfer][family]")
{
    const std::filesystem::path root = std::filesystem::path(__FILE__).parent_path();
    for (const auto* name : {
             "transfer_curve_ref_thresh10v0.csv",
             "transfer_curve_ref_thresh3v5.csv",
             "transfer_curve_ref_thresh2v8.csv",
             "transfer_curve_ref_thresh2v0.csv",
             "transfer_curve_ref_thresh0v0.csv",
         }) {
        const auto rows = loadRows(root / name);
        INFO("file=" << name);
        REQUIRE_FALSE(rows.empty());
    }
}

TEST_CASE("TransferFamily: five-curve ordering and minimum separation at checkpoints", "[transfer][family][conformance]")
{
    const std::filesystem::path root = std::filesystem::path(__FILE__).parent_path();
    const auto base = loadRows(root / "transfer_curve_ref_thresh10v0.csv");
    const auto c35  = loadRows(root / "transfer_curve_ref_thresh3v5.csv");
    const auto c28  = loadRows(root / "transfer_curve_ref_thresh2v8.csv");
    const auto c20  = loadRows(root / "transfer_curve_ref_thresh2v0.csv");
    const auto c00  = loadRows(root / "transfer_curve_ref_thresh0v0.csv");

    REQUIRE_FALSE(base.empty());
    REQUIRE_FALSE(c35.empty());
    REQUIRE_FALSE(c28.empty());
    REQUIRE_FALSE(c20.empty());
    REQUIRE_FALSE(c00.empty());

    const std::vector<float> checkpoints = {-12.0f, -9.0f, -6.0f, -3.0f, 0.0f, 3.0f, 6.0f};

    constexpr float kMinSep35To20 = 0.20f;
    constexpr float kMinSep20To00 = 0.50f;

    for (const float cp : checkpoints) {
        const float gr10 = nearestRow(base, cp).grDb;
        const float gr35 = nearestRow(c35, cp).grDb;
        const float gr28 = nearestRow(c28, cp).grDb;
        const float gr20 = nearestRow(c20, cp).grDb;
        const float gr00 = nearestRow(c00, cp).grDb;

        INFO("cp=" << cp << " gr10=" << gr10 << " gr35=" << gr35 << " gr28=" << gr28 << " gr20=" << gr20 << " gr00=" << gr00);

        REQUIRE(gr35 >= gr10 - 0.5f);
        REQUIRE(gr20 >= gr35 - 0.5f);
        REQUIRE(gr00 >= gr20 - 0.5f);

        REQUIRE((gr20 - gr35) >= kMinSep35To20);
        REQUIRE((gr00 - gr20) >= kMinSep20To00);
    }
}
