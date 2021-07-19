#pragma once

#include <memory>
#include "nhope/async/strand-executor.h"

namespace nhope::detail {

enum HolderOwnMode
{
    HolderOwns,
    HolderNotOwns
};

class HolderDeleter final
{
public:
    HolderDeleter(HolderOwnMode ownMode)
      : m_ownMode(ownMode)
    {}

    void operator()(SequenceExecutor* executor) const
    {
        if (m_ownMode == HolderOwns) {
            delete executor;   // NOLINT(cppcoreguidelines-owning-memory)
        }
    }

private:
    const HolderOwnMode m_ownMode;
};

using ExecutorHolder = std::unique_ptr<SequenceExecutor, HolderDeleter>;

inline ExecutorHolder makeStrand(Executor& executor)
{
    /* Small optimization. We create StrandExecutor only if passed executor is not SequenceExecutor */
    if (auto* seqExecutor = dynamic_cast<SequenceExecutor*>(&executor)) {
        return ExecutorHolder{seqExecutor, HolderNotOwns};
    }
    return ExecutorHolder{new StrandExecutor(executor), HolderOwns};
}

}   // namespace nhope::detail
