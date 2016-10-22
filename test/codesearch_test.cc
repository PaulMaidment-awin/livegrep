#include <string.h>
#include "gtest/gtest.h"

#include "src/codesearch.h"
#include "src/content.h"
#include "src/tools/grpc_server.h"

class codesearch_test : public ::testing::Test {
protected:
    codesearch_test() {
        cs_.set_alloc(make_mem_allocator());
        tree_ = cs_.open_tree("REPO", 0, "REV0");
    }

    code_searcher cs_;
    const indexed_tree *tree_;
};

const char *file1 = "The quick brown fox\n" \
    "jumps over the lazy\n\n\n" \
    "dog.\n";

TEST_F(codesearch_test, IndexTest) {
    cs_.index_file(tree_, "/data/file1", file1);
    cs_.finalize();

    EXPECT_EQ(1, cs_.end_files() - cs_.begin_files());

    indexed_file *f = *cs_.begin_files();
    EXPECT_EQ("/data/file1", f->path);
    EXPECT_EQ(tree_, f->tree);

    string content;

    for (auto it = f->content->begin(cs_.alloc());
         it != f->content->end(cs_.alloc()); ++it) {
        content += it->ToString();
        content += "\n";
    }

    EXPECT_EQ(string(file1), content);
}

TEST_F(codesearch_test, NoTrailingNewLine) {
    cs_.index_file(tree_, "/data/file1", "no newline");
    cs_.finalize();

    EXPECT_EQ(1, cs_.end_files() - cs_.begin_files());

    indexed_file *f = *cs_.begin_files();
    EXPECT_EQ("/data/file1", f->path);
    EXPECT_EQ(tree_, f->tree);

    string content;

    for (auto it = f->content->begin(cs_.alloc());
         it != f->content->end(cs_.alloc()); ++it) {
        content += it->ToString();
        content += "\n";
    }

    EXPECT_EQ(string("no newline\n"), content);
}

TEST_F(codesearch_test, DuplicateLinesInFile) {
    cs_.index_file(tree_, "/data/file1",
                   "line 1\n"
                   "line 1\n"
                   "line 2\n");
    cs_.finalize();

    CodeSearchImpl srv(&cs_, nullptr);
    Query request;
    CodeSearchResult matches;
    request.set_line("line 1");

    grpc::ServerContext ctx;

    grpc::Status st = srv.Search(&ctx, &request, &matches);
    ASSERT_TRUE(st.ok());

    ASSERT_EQ(2, matches.results_size());
    EXPECT_EQ(1, matches.results(0).line_number());
    EXPECT_EQ(2, matches.results(1).line_number());
}

TEST_F(codesearch_test, LongLines) {
    string xs = "x";
    for (int i = 0; i < 10; i++)
        xs += xs;

    cs_.index_file(tree_, "/data/file1",
                   string("line 1\n") +
                   string("NEEDLE|this line is over 1024 characters") + xs + string("\n") +
                   string("line 3\n") +
                   string("NEEDLE\n"));
    cs_.finalize();

    CodeSearchImpl srv(&cs_, nullptr);
    Query request;
    CodeSearchResult matches;
    request.set_line("NEEDLE");

    grpc::ServerContext ctx;

    grpc::Status st = srv.Search(&ctx, &request, &matches);
    ASSERT_TRUE(st.ok());

    ASSERT_EQ(1, matches.results_size());
    EXPECT_EQ(4, matches.results(0).line_number());
}


TEST_F(codesearch_test, RestrictFiles) {
    // tree_ is "REPO"
    cs_.index_file(tree_, "/file1", "contents");
    cs_.index_file(tree_, "/file2", "contents");
    // other is "OTHER"
    const indexed_tree *other = cs_.open_tree("OTHER", 0, "REV0");
    cs_.index_file(other, "/file1", "contents");
    cs_.index_file(other, "/file2", "contents");
    cs_.finalize();

    CodeSearchImpl srv(&cs_, nullptr);
    Query request;
    CodeSearchResult matches;
    grpc::ServerContext ctx;
    grpc::Status st;

    request.set_line("contents");
    request.set_file("file1");

    st = srv.Search(&ctx, &request, &matches);
    ASSERT_TRUE(st.ok());

    ASSERT_EQ(2, matches.results_size());
    EXPECT_EQ("/file1", matches.results(0).path());
    EXPECT_EQ("/file1", matches.results(1).path());

    request.clear_file();
    request.set_repo("REPO");

    matches.Clear();
    st = srv.Search(&ctx, &request, &matches);
    ASSERT_TRUE(st.ok());

    ASSERT_EQ(2, matches.results_size());
    EXPECT_EQ("REPO", matches.results(0).tree());
    EXPECT_EQ("REPO", matches.results(1).tree());

    request.clear_repo();
    request.set_not_file("file1");

    matches.Clear();
    st = srv.Search(&ctx, &request, &matches);
    ASSERT_TRUE(st.ok());

    ASSERT_EQ(2, matches.results_size());
    EXPECT_EQ("/file2", matches.results(0).path());
    EXPECT_EQ("/file2", matches.results(1).path());
}
