// Pins MaskEditHistory: undo and redo restore the buffer exactly, the dirty flag follows the
// saved state through undo and redo, and the byte cap drops the oldest operations first.
#include "MaskEditHistory.h"

#include <cstdio>
#include <random>
#include <vector>

namespace
{

int failures = 0;

void check(bool condition, const char *what)
{
    std::printf("%-62s %s\n", what, condition ? "ok" : "FAIL");
    if (!condition)
        ++failures;
}

// One operation of random writes, some of which put a voxel back to what it held.
void randomOperation(MaskEditHistory &history, std::vector<int> &buffer, std::mt19937 &rng)
{
    std::uniform_int_distribution<std::size_t> voxel(0, buffer.size() - 1);
    std::uniform_int_distribution<int> label(0, 5);
    std::uniform_int_distribution<int> count(1, 200);
    history.begin("op");
    const int n = count(rng);
    for (int i = 0; i < n; ++i)
        history.write(buffer, voxel(rng), label(rng));
    history.commit();
}

void testRandomUndoRedo()
{
    std::mt19937 rng(7);
    std::vector<int> buffer(4096);
    std::uniform_int_distribution<int> label(0, 5);
    for (int &v : buffer)
        v = label(rng);

    MaskEditHistory history;
    std::vector<std::vector<int>> states{buffer};
    for (int i = 0; i < 300; ++i)
    {
        const std::vector<int> before = buffer;
        randomOperation(history, buffer, rng);
        if (buffer != before)
            states.push_back(buffer);
    }
    check(history.undoCount() == states.size() - 1, "one operation per change of the buffer");

    bool everyUndo = true;
    for (std::size_t k = states.size() - 1; k > 0; --k)
        everyUndo = history.undo(buffer) && buffer == states[k - 1] && everyUndo;
    check(everyUndo, "each undo restores the state before its operation");
    check(!history.undo(buffer), "nothing left to undo at the start");
    check(!history.isDirty(), "undoing everything returns to the saved state");

    bool everyRedo = true;
    for (std::size_t k = 1; k < states.size(); ++k)
        everyRedo = history.redo(buffer) && buffer == states[k] && everyRedo;
    check(everyRedo, "each redo restores the state after its operation");
    check(history.isDirty(), "the redone buffer differs from the saved one");

    // A random walk of undo and redo stays on the recorded states.
    std::size_t position = states.size() - 1;
    std::bernoulli_distribution undoStep(0.5);
    bool walk = true;
    for (int i = 0; i < 2000; ++i)
    {
        if (undoStep(rng))
        {
            if (history.undo(buffer))
                --position;
        }
        else if (history.redo(buffer))
            ++position;
        walk = walk && buffer == states[position];
    }
    check(walk, "a random undo/redo walk stays on the recorded states");
}

void testNoOpOperations()
{
    std::vector<int> buffer(16, 3);
    MaskEditHistory history;
    history.begin("same value");
    history.write(buffer, 4, 3);
    check(!history.commit(), "a write of the value already there is no operation");

    history.begin("there and back");
    history.write(buffer, 4, 9);
    history.write(buffer, 4, 3);
    check(!history.commit(), "a voxel written back to its value is no operation");
    check(!history.canUndo() && !history.isDirty(), "no-op operations leave the history clean");

    history.begin("Brush");
    history.write(buffer, 1, 7);
    history.begin("Brush");
    history.write(buffer, 2, 7);
    history.begin("Erase");
    history.write(buffer, 2, 0);
    history.commit();
    check(history.undoCount() == 2, "begin with the open name continues it; another name closes it");
    check(history.undoName() == "Erase", "the name of the operation to undo");
}

void testDirtyTracking()
{
    std::vector<int> buffer(64, 0);
    MaskEditHistory history;
    for (int i = 0; i < 3; ++i)
    {
        history.begin("paint");
        history.write(buffer, std::size_t(i), 1);
        history.commit();
    }
    check(history.isDirty(), "edits make the buffer dirty");
    history.markSaved();
    check(!history.isDirty(), "a save makes it clean");
    history.undo(buffer);
    check(history.isDirty(), "undoing past the save makes it dirty");
    history.redo(buffer);
    check(!history.isDirty(), "redoing back to the save makes it clean");

    history.undo(buffer);
    history.begin("paint");
    history.write(buffer, 10, 2);
    history.commit();
    check(!history.canRedo(), "a new operation drops the redo stack");
    while (history.undo(buffer))
    {
    }
    check(history.isDirty(), "a saved state dropped with the redo stack is not reachable again");

    history.reset(false);
    check(history.isDirty() && !history.canUndo(), "reset(false): nothing to undo, not saved");
    history.reset(true);
    check(!history.isDirty(), "reset(true): the buffer is the saved state");

    history.begin("open");
    history.write(buffer, 20, 4);
    check(history.isDirty() && history.canUndo(), "an open operation with changes counts as dirty");
    check(history.undo(buffer) && buffer[20] == 0, "undo commits the open operation first");
}

void testByteCap()
{
    constexpr std::size_t kVoxels = 100;
    std::vector<int> buffer(kVoxels * 40, 0);
    // About four operations of 100 changes fit.
    MaskEditHistory history(4 * (kVoxels * 16 + 128));
    std::vector<std::vector<int>> states{buffer};
    for (int op = 0; op < 20; ++op)
    {
        history.begin("block");
        for (std::size_t i = 0; i < kVoxels; ++i)
            history.write(buffer, op * kVoxels + i, op + 1);
        history.commit();
        states.push_back(buffer);
        if (op == 16)
            history.markSaved();
    }
    check(history.bytes() <= history.byteCap(), "the history stays under its cap");
    check(history.undoCount() < 20 && history.undoCount() >= 2, "the oldest operations were dropped");

    const std::size_t kept = history.undoCount();
    while (history.undo(buffer))
    {
    }
    check(buffer == states[20 - kept], "undoing everything kept reaches the oldest reachable state");

    for (std::size_t i = 0; i < kept; ++i)
        history.redo(buffer);
    check(buffer == states[20], "redo returns to the newest state");

    MaskEditHistory tiny(1);
    std::vector<int> big(1000, 0);
    tiny.begin("huge");
    for (std::size_t i = 0; i < big.size(); ++i)
        tiny.write(big, i, 1);
    tiny.commit();
    check(tiny.undoCount() == 1 && tiny.undo(big) && big[999] == 0,
          "an operation larger than the cap is still undoable");
}

void testSavedStateDroppedByCap()
{
    std::vector<int> buffer(2000, 0);
    MaskEditHistory history(3 * (100 * 16 + 128));
    history.begin("first");
    for (std::size_t i = 0; i < 100; ++i)
        history.write(buffer, i, 1);
    history.commit();
    history.markSaved();
    const std::vector<int> saved = buffer;
    for (int op = 1; op < 8; ++op)
    {
        history.begin("more");
        for (std::size_t i = 0; i < 100; ++i)
            history.write(buffer, op * 100 + i, 2);
        history.commit();
    }
    // The saved state is the result of an operation the cap has dropped: undoing back to
    // the oldest reachable state is the saved state only if that operation was the last one
    // dropped, which it is not here.
    while (history.undo(buffer))
    {
    }
    check(history.isDirty() == (buffer != saved), "dirty matches whether the buffer equals the saved one");
}

void testRecordAndMismatch()
{
    std::vector<int> buffer(32, 0);
    MaskEditHistory history;
    const std::vector<std::size_t> indices{3, 4, 5, 6};
    std::vector<int> before;
    for (std::size_t i : indices)
        before.push_back(buffer[i]);
    buffer[4] = 2; // an external write, as a slice paste does
    buffer[5] = 2;
    check(history.record(buffer, indices, before, "Slice segmentation"), "record() keeps an external write");
    check(history.undo(buffer) && buffer[4] == 0 && buffer[5] == 0, "and undoes it");
    check(!history.record(buffer, indices, std::vector<int>(4, 0), "nothing"), "an unchanged region is no operation");

    history.redo(buffer);
    std::vector<int> smaller(4, 0);
    check(!history.undo(smaller) && smaller == std::vector<int>(4, 0),
          "undo refuses a buffer smaller than the operation and leaves it alone");
    check(history.canUndo(), "and keeps the operation");
}

// Per-label deltas: summed over operations they must equal the change of a histogram.
void testLabelDelta()
{
    std::mt19937 rng(11);
    std::vector<int> buffer(2048);
    std::uniform_int_distribution<int> label(0, 6);
    for (int &v : buffer)
        v = label(rng);
    auto histogram = [](const std::vector<int> &b)
    {
        std::vector<long long> h(7, 0);
        for (int v : b)
            ++h[std::size_t(v)];
        return h;
    };
    MaskEditHistory history;
    std::vector<long long> counts = histogram(buffer);
    auto apply = [&counts](const std::unordered_map<int, long long> &delta)
    {
        for (const auto &[l, d] : delta)
            counts[std::size_t(l)] += d;
    };
    bool consistent = true;
    for (int i = 0; i < 100; ++i)
    {
        randomOperation(history, buffer, rng);
        apply(history.lastLabelDelta());
        consistent = consistent && counts == histogram(buffer);
        if (i % 3 == 0 && history.undo(buffer))
        {
            apply(history.lastLabelDelta());
            consistent = consistent && counts == histogram(buffer);
            history.redo(buffer);
            apply(history.lastLabelDelta());
            consistent = consistent && counts == histogram(buffer);
        }
    }
    check(consistent, "label deltas of commit, undo and redo track a histogram");
}

} // namespace

int main()
{
    testRandomUndoRedo();
    testNoOpOperations();
    testDirtyTracking();
    testByteCap();
    testSavedStateDroppedByCap();
    testRecordAndMismatch();
    testLabelDelta();
    std::printf("%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
