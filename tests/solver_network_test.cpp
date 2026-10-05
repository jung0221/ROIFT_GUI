// Checks on reading a 1D solver network and finding its segments in a volume.
// The YAML is written by vessels.cli.analyze_vessels in the layout of the
// virtualPatient solver files; a misread here shows the wrong numbers beside
// the right vessel, which nothing downstream would catch.
#include "MaskLayers.h"
#include "SolverNetwork.h"

#include <QCoreApplication>
#include <array>
#include <cmath>
#include <cstdio>

namespace
{

int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("%-62s %s\n", what, condition ? "ok" : "FAIL");
    if (!condition)
        ++failures;
}

const char *kYaml = R"(project_name: case
inlet_file: case_solver_inletFlow.dat
write_results: [P, u, Q, A]

blood:
  rho: 1050.00  # density [kg/m^3]

network:
  - label: PulmonaryTrunk
    sn: 1 # start node
    tn: 2 # end node
    L: 1.860000e-02  # length [m]
    E: 4.000000e+05  # Young's modulus [Pa]
    R0: 7.130000e-03  # reference lumen radius [m]
    M: 5  # number of divisions

  - label: LPA_01
    sn: 2 # start node
    tn: 3 # end node
    L: 4.360000e-02  # length [m]
    R0: 4.910000e-03  # reference lumen radius [m]
    # Boundary Condition: 3E_Windkessel
    R1: 1.500000e+08  # 1st peripheral resistance
    inlet_impedance_matching: true

  - label: RPA_01
    sn: 2
    tn: 4
    L: 5.350000e-02
    R0: 5.550000e-03
    R1: 1.500000e+08
)";

// Not "near": windef.h defines near as an empty macro on MSVC.
bool approxEqual(double a, double b) { return std::fabs(a - b) <= 1e-12 * std::max(1.0, std::fabs(b)); }

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    SolverNetwork net;
    QString error;
    check(parseSolverNetwork(QString::fromUtf8(kYaml), net, &error), "parses the analyze_vessels layout");
    check(net.segments.size() == 3, "three segments");
    check(net.headerValue("project_name") == "case", "top-level scalar");
    check(net.headerValue("blood.rho") == "1050.00", "nested scalar, comment stripped");
    check(net.headerValue("write_results") == "[P, u, Q, A]", "flow list kept as written");

    const SolverSegment &trunk = net.segments[0];
    check(trunk.index == 1 && trunk.label == "PulmonaryTrunk", "index is the 1-based file position");
    check(trunk.sn == 1 && trunk.tn == 2, "nodes");
    check(approxEqual(trunk.lengthM, 0.0186) && approxEqual(trunk.radiusM, 0.00713), "L and R0 in metres");
    check(approxEqual(trunk.youngPa, 4.0e5), "E");
    check(!trunk.outlet && net.segments[1].outlet, "outlet = has R1");
    check(net.parentOf(0) == -1 && net.parentOf(1) == 0, "parent by node");
    check(net.daughtersOf(0) == std::vector<int>({1, 2}), "daughters by node");
    check(net.describe(1).contains("from: PulmonaryTrunk") && net.describe(1).contains("to: outlet"),
          "description names parent and outlet");
    check(net.describe(1).contains("R1: 1.500000e+08"), "description keeps the boundary condition");

    SolverNetwork bad;
    check(!parseSolverNetwork("network:\n  - label: X\n    sn: 1\n", bad, &error) && error.contains("X"),
          "a segment without L/R0/tn is refused, by name");
    check(!parseSolverNetwork("project_name: x\n", bad, &error), "no network is refused");

    check(solverSegmentMapPath("/d/case_artery_solver.yaml") == "/d/case_artery_solver_segments.nii.gz",
          "segment map beside the yaml");

    // 4x3x2 volume: segment 1 along x at y=0,z=0; segment 2 a single voxel.
    MaskVolume vol;
    vol.dimX = 4;
    vol.dimY = 3;
    vol.dimZ = 2;
    vol.data.assign(24, 0);
    vol.data[0] = vol.data[1] = vol.data[2] = 1;
    vol.data[(1 * 3 + 2) * 4 + 3] = 2; // x3 y2 z1
    SegmentVoxelIndex index;
    index.build(vol, 3);
    check(index.segmentAt(1, 0, 0) == 1 && index.segmentAt(3, 2, 1) == 2, "voxel lookup");
    check(index.segmentAt(3, 0, 0) == 0 && index.segmentAt(9, 0, 0) == 0, "background and out of range");
    check(index.segmentNear(3, 1, 1, 1) == 2, "nearest within a radius");
    const SegmentAnchor &a = index.anchors()[0];
    check(a.valid && a.x == 1 && a.y == 0 && a.z == 0 && a.voxels == 3, "anchor is the member nearest the centroid");
    check(!index.anchors()[2].valid, "a segment with no voxels has no anchor");

    // Geometry: the file analyze_vessels writes beside the YAML.
    const QByteArray yamlBytes = QByteArray(kYaml);
    SolverNetwork hashed;
    parseSolverNetwork(QString::fromUtf8(yamlBytes), hashed);
    hashed.rawText = yamlBytes;
    const QString sha = hashed.textSha256();
    QByteArray crlf = yamlBytes;
    crlf.replace("\n", "\r\n");
    SolverNetwork windows = hashed;
    windows.rawText = crlf;
    check(windows.textSha256() == sha, "hash ignores CRLF, as the exporter hashed the string");

    auto geometry = [&](const QString &shaText, const char *thirdLabel, int shapeX)
    {
        return QString(R"({"format": "ctsegmentation.solver_geometry", "version": 1, "yaml_sha256": "%1",
          "files": {"segment_map": "s.nii.gz"}, "grid": {"shape": [%3, 3, 2], "spacing_mm": [0.5, 0.5, 1.5]},
          "nodes": [{"id": 1, "voxel": [0, 0, 0]}, {"id": 2, "voxel": [1, 0, 0]},
                    {"id": 3, "voxel": [2, 0, 0.5]}, {"id": 4, "voxel": [3, 1, 1]}],
          "segments": [
            {"index": 1, "label": "PulmonaryTrunk", "sn": 1, "tn": 2, "lumen_voxels": 4, "territory_voxels": 9,
             "centerline": [[0, 0, 0], [0.5, 0, 0], [1, 0, 0]]},
            {"index": 2, "label": "LPA_01", "sn": 2, "tn": 3, "centerline": [[1, 0, 0], [2, 0, 0.5]]},
            {"index": 3, "label": "%2", "sn": 2, "tn": 4, "centerline": [[1, 0, 0], [3, 1, 1]]}]})")
            .arg(shaText, QString::fromLatin1(thirdLabel))
            .arg(shapeX)
            .toUtf8();
    };
    const unsigned int dims[3] = {4, 3, 2};
    SolverGeometry geo;
    check(parseSolverGeometry(geometry(sha, "RPA_01", 4), geo, &error), "geometry parses");
    check(geo.valid && geo.nodes.size() == 4 && geo.segments.size() == 3, "nodes and segments read");
    check(geo.node(3) && std::fabs(geo.node(3)->z - 0.5) < 1e-9, "fractional node position kept");
    check(geo.segments[0].centerline.size() == 3 && geo.segments[0].territoryVoxels == 9, "centreline and counts");
    check(geo.files.value("segment_map") == "s.nii.gz" && std::fabs(geo.spacing[2] - 1.5) < 1e-9, "files and spacing");
    check(solverGeometryProblems(hashed, geo, dims).isEmpty(), "consistent geometry has no problems");
    check(!solverGeometryProblems(hashed, geo, std::array<unsigned int, 3>{5, 3, 2}.data()).isEmpty(),
          "another grid is a problem");

    SolverGeometry stale;
    parseSolverGeometry(geometry(QString(64, '0'), "RPA_01", 4), stale);
    check(solverGeometryProblems(hashed, stale, dims).join(" ").contains("different YAML"), "edited YAML is caught");
    SolverGeometry renamed;
    parseSolverGeometry(geometry(QString(), "RB_01", 4), renamed);
    check(solverGeometryProblems(hashed, renamed, dims).join(" ").contains("RB_01"),
          "a segment named differently is caught, even without a hash");
    SolverGeometry other;
    check(!parseSolverGeometry(R"({"format": "something.else", "version": 1})", other, &error) &&
              error.contains("not a solver geometry"),
          "another JSON is refused");
    check(!parseSolverGeometry(R"({"format": "ctsegmentation.solver_geometry", "version": 99})", other, &error) &&
              error.contains("99"),
          "a newer version is refused");

    check(solverNodeKind(net, 1) == SolverNodeKind::Inlet && solverNodeKind(net, 2) == SolverNodeKind::Junction &&
              solverNodeKind(net, 3) == SolverNodeKind::Outlet && solverNodeKind(net, 99) == SolverNodeKind::Unknown,
          "node kinds from topology");
    check(net.segmentsInto(2) == std::vector<int>({0}) && net.segmentsOutOf(2) == std::vector<int>({1, 2}),
          "segments in and out of a node");
    check(net.nodeIds() == std::vector<int>({1, 2, 3, 4}), "node ids");

    std::printf("\n%s\n", failures ? "FAILURES" : "all solver-network checks passed");
    return failures ? 1 : 0;
}
