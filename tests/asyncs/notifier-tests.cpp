#include <climits>

#include <gtest/gtest.h>

#include <boost/asio.hpp>

#include <nhope/asyncs/notifier.h>
#include <nhope/asyncs/func-produser.h>

using namespace nhope::asyncs;

TEST(NotifierTests, CreateDestroyNotifier)   // NOLINT
{
    constexpr int IterCount = 100;
    constexpr int MaxNotifyCount = 1000;

    FuncProduser<int> numProduser([m = 0](int& value) mutable -> bool {
        value = m++;
        return true;
    });
    numProduser.start();

    for (int i = 0; i < IterCount; ++i) {
        boost::asio::io_context ioCtx;
        auto workGuard = boost::asio::make_work_guard(ioCtx);

        Notifier notifer(ioCtx, std::function([notifyCount = 0, &ioCtx](const int& /*unused*/) mutable {
                             if (++notifyCount > MaxNotifyCount) {
                                 ioCtx.stop();
                             }
                         }));

        notifer.attachToProduser(numProduser);

        ioCtx.run();
    }
}
