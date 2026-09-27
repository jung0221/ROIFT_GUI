// Checks on reading a 1D solver network and finding its segments in a volume.
// The YAML is written by vessels.cli.analyze_vessels in the layout of the
// virtualPatient solver files; a misread here shows the wrong numbers beside
// the right vessel, which nothing downstream would catch.
#include "MaskLayers.h"
#include "SolverNetwork.h"

#include <QCoreApplication>
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

bool near(double a, double b) { return std::fabs(a - b) <= 1e-12 * std::max(1.0, std::fabs(b)); }

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
    check(near(trunk.lengthM, 0.0186) && near(trunk.radiusM, 0.00713), "L and R0 in metres");
    check(near(trunk.youngPa, 4.0e5), "E");
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

    std::printf("\n%s\n", failures ? "FAILURES" : "all solver-network checks passed");
    return failures ? 1 : 0;
}
