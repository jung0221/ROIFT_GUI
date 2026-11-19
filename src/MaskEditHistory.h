#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

// Undo history of one label buffer, free of Qt and of the window. An operation keeps the
// value before and after of each voxel it changed, so undo and redo replay a sparse diff;
// a copy per operation would be 1.27 GB for an int mask of 704 x 704 x 640.
class MaskEditHistory
{
public:
    static constexpr std::size_t kDefaultByteCap = std::size_t(512) << 20;

    explicit MaskEditHistory(std::size_t byteCap = kDefaultByteCap);

    // Opens an operation, or continues the open one when it has the same name, so one
    // brush stroke is one operation however many pointer events it takes.
    void begin(const std::string &name);
    // Sets buffer[index] in the open operation, opening an unnamed one if needed. A write
    // that leaves the voxel as it was is not recorded.
    void write(std::vector<int> &buffer, std::size_t index, int value);
    // Closes the open operation. False when it changed nothing; it is then dropped.
    bool commit();
    bool hasOpenOperation() const { return m_hasOpen; }

    // Records, as one operation, a write made without write(): before[i] is what
    // buffer[indices[i]] held ahead of it. Commits an open operation first.
    bool record(const std::vector<int> &buffer, const std::vector<std::size_t> &indices,
                const std::vector<int> &before, const std::string &name);

    // Both commit an open operation first, and refuse, leaving the buffer as it is, when
    // the buffer is too small for the operation (it is not the buffer the history is of).
    bool undo(std::vector<int> &buffer);
    bool redo(std::vector<int> &buffer);
    bool canUndo() const { return !m_undo.empty() || (m_hasOpen && !m_open.changes.empty()); }
    bool canRedo() const { return !m_redo.empty(); }
    std::string undoName() const;
    std::string redoName() const;
    std::size_t undoCount() const { return m_undo.size(); }
    std::size_t redoCount() const { return m_redo.size(); }
    // Name and changed-voxel count of the operation the last undo or redo replayed.
    std::size_t lastReplayedVoxels() const { return m_lastReplayed; }
    // Voxels gained (positive) or lost per label by the last commit, record, undo or redo
    // that changed the buffer, so per-label counts follow an edit without a rescan.
    const std::unordered_map<int, long long> &lastLabelDelta() const { return m_lastDelta; }

    // Forgets every operation. The buffer as it now stands is the saved state when saved
    // is true, and otherwise differs from it until markSaved().
    void reset(bool saved);
    // The buffer as it now stands is what was written to disk.
    void markSaved();
    // True when the buffer differs from the saved state. Undoing back to the saved state
    // makes it false again; once that state has dropped out of the history, only a save does.
    bool isDirty() const;

    std::size_t bytes() const { return m_bytes; }
    std::size_t byteCap() const { return m_byteCap; }

private:
    struct Change
    {
        std::uint64_t index;
        int before;
        int after;
    };
    struct Operation
    {
        std::uint64_t id = 0;
        std::string name;
        std::vector<Change> changes;
        std::uint64_t maxIndex = 0;
    };

    static constexpr std::uint64_t kUnreachable = UINT64_MAX;

    static void compact(Operation &op);
    static std::size_t operationBytes(const Operation &op);
    void push(Operation &&op);
    void setDelta(const Operation &op, bool forward);
    void enforceCap();
    std::uint64_t stateId() const;

    std::size_t m_byteCap;
    std::deque<Operation> m_undo; // oldest first
    std::vector<Operation> m_redo; // most recently undone last
    Operation m_open;
    bool m_hasOpen = false;
    std::uint64_t m_nextId = 1;
    // Identifies the buffer when the undo stack is empty: 0 at reset, then the id of the last
    // operation dropped by the cap, whose result is the oldest state still reachable.
    std::uint64_t m_baseId = 0;
    std::uint64_t m_savedId = 0;
    std::size_t m_bytes = 0;
    std::size_t m_lastReplayed = 0;
    std::unordered_map<int, long long> m_lastDelta;
};
