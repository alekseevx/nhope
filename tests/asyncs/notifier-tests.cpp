#include <limits.h>

#include <gtest/gtest.h>

#include <boost/asio.hpp>

#include <nhope/asyncs/notifier.h>
#include <nhope/asyncs/func-produser.h>

using namespace nhope::asyncs;

TEST(NotifierTests, CreateDestroyNotifier)
{
    FuncProduser<int> numProduser([m = 0](int& value) mutable -> bool {
        value = m++;
        return true;
    });
    numProduser.start();

    for (int i = 0; i < 100; ++i) {
        boost::asio::io_context ioCtx;
        auto workGuard = boost::asio::make_work_guard(ioCtx);

        Notifier notifer(ioCtx, std::function([m = 1000, &ioCtx](const int&) mutable {
                             if (--m <= 0) {
                                 ioCtx.stop();
                             }
                         }));

        notifer.attachToProduser(numProduser);

        ioCtx.run();
    }
}
