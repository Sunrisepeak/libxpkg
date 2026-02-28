#include <gtest/gtest.h>

import mcpplibs.templates;

TEST(TemplatesTest, HelloMcpp) {
    testing::internal::CaptureStdout();
    mcpplibs::templates::hello_mcpp();
    std::string output = testing::internal::GetCapturedStdout();
    EXPECT_EQ(output, "hello mcpp!\n");
}

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
