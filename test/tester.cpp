#include <gtest/gtest.h>

#include "mlvlib/mlvlib.hpp"

TEST(SkeletonTest, LibraryName)
{
    EXPECT_STREQ(mlvlib::library_name(), "mlvlib");
}

int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
