#include "pipeline.hpp"

#include <gtest/gtest.h>

using namespace tala;
using namespace tala::pipeline;

namespace {

struct Case {
    const char* in;
    const char* out;
};

void PrintTo(const Case& c, std::ostream* os) {
    *os << '"' << c.in << '"';
}

PipelineOptions plain() {
    return {};
}

} // namespace

class Run : public testing::TestWithParam<Case> {};

TEST_P(Run, Default) {
    EXPECT_EQ(run(QString::fromUtf8(GetParam().in), plain()).toStdString(), GetParam().out);
}

INSTANTIATE_TEST_SUITE_P(Pipeline, Run, testing::Values(
    // Untouched.
    Case{"Let's meet on Wednesday at noon.", "Let's meet on Wednesday at noon."},
    // Fillers and stutters.
    Case{"Um, so basically we we should ship it this week.", "So basically we should ship it this week."},
    Case{"I think, uh, it works.", "I think it works."},
    Case{"I know that that is true.", "I know that that is true."},
    // Spoken layout and marks.
    Case{"Hello. New line. How are you?", "Hello.\nHow are you?"},
    Case{"Dear Sam, new paragraph, thanks for the notes.", "Dear Sam\n\nThanks for the notes."},
    Case{"Dear Sam comma new paragraph thanks for the notes.", "Dear Sam,\n\nThanks for the notes."},
    Case{"Is it ready question mark", "Is it ready?"},
    Case{"Put a comma here.", "Put a comma here."},
    Case{"First comma second period", "First, second."},
    // Taking it back.
    Case{"I'll be there at five. Scratch that. I'll be there at six.", "I'll be there at six."},
    Case{"Scratch that. Start over.", "Start over."},
    Case{"Okay. Meet at five, scratch that, meet at six.", "Okay. Meet at six."}));

TEST(Pipeline, Corrections) {
    const QList<Correction> rules{{"Quillon", "Quilon"}, {"don", "Dawn"}, {"open ai", "OpenAI"}};
    EXPECT_EQ(corrections("Ask Quillon about it", rules), "Ask Quilon about it");
    EXPECT_EQ(corrections("Don said I don't know", rules), "Dawn said I don't know");
    EXPECT_EQ(corrections("Don's car", rules), "Dawn's car");
    EXPECT_EQ(corrections("Use Open AI's model", rules), "Use OpenAI's model");
    EXPECT_EQ(corrections("Donation", rules), "Donation");
}

TEST(Pipeline, Snippets) {
    const QList<SnippetRule> rules{{"my email signature", "Best,\nSam"}, {"my link", "https://cal.com/sam"}};
    EXPECT_EQ(snippets("My email signature.", rules), "Best,\nSam");
    EXPECT_EQ(snippets("Book here: my link, thanks.", rules), "Book here: https://cal.com/sam, thanks.");
    EXPECT_EQ(snippets("my linked list", rules), "my linked list");
}

TEST(Pipeline, Style) {
    EXPECT_EQ(style("Sounds good. See you then.", Style::Formal), "Sounds good. See you then.");
    EXPECT_EQ(style("Sounds good. See you then.", Style::Casual), "Sounds good. See you then");
    EXPECT_EQ(style("Are you coming?", Style::Casual), "Are you coming?");
    EXPECT_EQ(style("Sounds good. I think NASA called.", Style::VeryCasual), "sounds good. I think NASA called");
}

TEST(Pipeline, Continuing) {
    EXPECT_EQ(continuing("First part.", "Second part."), " Second part.");
    EXPECT_EQ(continuing("Line\n", "Next"), "Next");
    EXPECT_EQ(continuing({}, "Start"), "Start");
}
