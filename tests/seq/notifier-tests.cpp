#include <climits>

#include <gtest/gtest.h>

#include <asio.hpp>

#include <nhope/seq/notifier.h>
#include <nhope/seq/func-produser.h>

using namespace nhope;

TEST(NotifierTests, CreateDestroyNotifier)   // NOLINT
{
    static constexpr int IterCount = 100;
    static constexpr int MaxNotifyCount = 1000;

    FuncProduser<int> numProduser([m = 0](int& value) mutable -> bool {
        value = m++;
        return true;
    });
    numProduser.start();

    for (int i = 0; i < IterCount; ++i) {
        asio::io_context ioCtx;
        auto workGuard = asio::make_work_guard(ioCtx);

        Notifier notifier(ioCtx, std::function([notifyCount = 0, &ioCtx](const int& /*unused*/) mutable {
                              if (++notifyCount > MaxNotifyCount) {
                                  ioCtx.stop();
                              }
                          }));

        notifier.attachToProduser(numProduser);

        ioCtx.run();
    }
}
