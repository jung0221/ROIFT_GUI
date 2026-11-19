#include "MaskEditHistory.h"

#include <algorithm>
#include <utility>

MaskEditHistory::MaskEditHistory(std::size_t byteCap) : m_byteCap(byteCap) {}

void MaskEditHistory::begin(const std::string &name)
{
    if (m_hasOpen && m_open.name == name)
        return;
    commit();
    m_open = Operation{};
    m_open.name = name;
    m_hasOpen = true;
}

void MaskEditHistory::write(std::vector<int> &buffer, std::size_t index, int value)
{
    if (!m_hasOpen)
        begin("Edit");
    const int before = buffer[index];
    if (before == value)
        return;
    buffer[index] = value;
    m_open.changes.push_back({index, before, value});
}

void MaskEditHistory::compact(Operation &op)
{
    // A voxel written twice keeps its first before and its last after; stable, so the
    // first write stays first among equal indices.
    std::stable_sort(op.changes.begin(), op.changes.end(),
                     [](const Change &a, const Change &b)
                     { return a.index < b.index; });
    std::vector<Change> merged;
    merged.reserve(op.changes.size());
    for (std::size_t i = 0; i < op.changes.size();)
    {
        std::size_t j = i;
        while (j + 1 < op.changes.size() && op.changes[j + 1].index == op.changes[i].index)
            ++j;
        const Change change{op.changes[i].index, op.changes[i].before, op.changes[j].after};
        if (change.before != change.after)
            merged.push_back(change);
        i = j + 1;
    }
    merged.shrink_to_fit();
    op.changes = std::move(merged);
    op.maxIndex = op.changes.empty() ? 0 : op.changes.back().index;
}

std::size_t MaskEditHistory::operationBytes(const Operation &op)
{
    return sizeof(Operation) + op.name.size() + op.changes.capacity() * sizeof(Change);
}

bool MaskEditHistory::commit()
{
    if (!m_hasOpen)
        return false;
    m_hasOpen = false;
    Operation op = std::move(m_open);
    m_open = Operation{};
    compact(op);
    if (op.changes.empty())
        return false;
    push(std::move(op));
    return true;
}

bool MaskEditHistory::record(const std::vector<int> &buffer, const std::vector<std::size_t> &indices,
                             const std::vector<int> &before, const std::string &name)
{
    commit();
    if (indices.size() != before.size())
        return false;
    Operation op;
    op.name = name;
    for (std::size_t i = 0; i < indices.size(); ++i)
    {
        if (indices[i] >= buffer.size())
            return false;
        const int after = buffer[indices[i]];
        if (after != before[i])
            op.changes.push_back({indices[i], before[i], after});
    }
    compact(op);
    if (op.changes.empty())
        return false;
    push(std::move(op));
    return true;
}

void MaskEditHistory::push(Operation &&op)
{
    for (const Operation &dropped : m_redo)
        m_bytes -= operationBytes(dropped);
    m_redo.clear();
    op.id = m_nextId++;
    m_bytes += operationBytes(op);
    m_undo.push_back(std::move(op));
    enforceCap();
}

void MaskEditHistory::enforceCap()
{
    // The newest operation is kept whatever its size, so the last edit can always be undone.
    while (m_bytes > m_byteCap && m_undo.size() > 1)
    {
        m_baseId = m_undo.front().id;
        m_bytes -= operationBytes(m_undo.front());
        m_undo.pop_front();
    }
}

bool MaskEditHistory::undo(std::vector<int> &buffer)
{
    commit();
    if (m_undo.empty())
        return false;
    Operation op = std::move(m_undo.back());
    if (op.maxIndex >= buffer.size())
    {
        m_undo.back() = std::move(op);
        return false;
    }
    m_undo.pop_back();
    for (const Change &change : op.changes)
        buffer[change.index] = change.before;
    m_lastReplayed = op.changes.size();
    m_redo.push_back(std::move(op));
    return true;
}

bool MaskEditHistory::redo(std::vector<int> &buffer)
{
    commit();
    if (m_redo.empty())
        return false;
    Operation op = std::move(m_redo.back());
    if (op.maxIndex >= buffer.size())
    {
        m_redo.back() = std::move(op);
        return false;
    }
    m_redo.pop_back();
    for (const Change &change : op.changes)
        buffer[change.index] = change.after;
    m_lastReplayed = op.changes.size();
    m_undo.push_back(std::move(op));
    return true;
}

std::string MaskEditHistory::undoName() const
{
    if (m_hasOpen)
        return m_open.name;
    return m_undo.empty() ? std::string() : m_undo.back().name;
}

std::string MaskEditHistory::redoName() const
{
    return m_redo.empty() ? std::string() : m_redo.back().name;
}

void MaskEditHistory::reset(bool saved)
{
    m_undo.clear();
    m_redo.clear();
    m_open = Operation{};
    m_hasOpen = false;
    m_bytes = 0;
    m_lastReplayed = 0;
    // A fresh base id, so a state saved before the reset can never match after it.
    m_baseId = m_nextId++;
    m_savedId = saved ? m_baseId : kUnreachable;
}

void MaskEditHistory::markSaved()
{
    commit();
    m_savedId = stateId();
}

bool MaskEditHistory::isDirty() const
{
    if (m_hasOpen && !m_open.changes.empty())
        return true;
    return stateId() != m_savedId;
}

std::uint64_t MaskEditHistory::stateId() const
{
    return m_undo.empty() ? m_baseId : m_undo.back().id;
}
