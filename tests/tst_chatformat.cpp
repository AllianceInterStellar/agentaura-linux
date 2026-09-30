// SPDX-License-Identifier: MIT
//
// Agent replies render as Markdown, and nothing in a reply becomes live HTML.
#include "services/ChatFormat.h"

#include <QtTest>

class TstChatFormat : public QObject {
    Q_OBJECT
private slots:
    void rendersMarkdown() {
        const QString html = ChatFormat::markdownToHtml("**bold** and `code`\n\n- one\n- two");
        QVERIFY(html.contains("font-weight"));   // bold
        QVERIFY(html.contains("<li"));           // list
        QVERIFY(html.contains("code"));
        QVERIFY(!html.contains("**"));
    }

    void rendersCodeBlocks() {
        // How the block is serialised (<pre>, or a styled paragraph) differs between Qt 6.2 and
        // later releases; what matters is that it is read as a code block and kept verbatim.
        const QString html = ChatFormat::markdownToHtml("```\nint x = 1 < 2;\n```");
        QVERIFY2(!html.contains("```"), qPrintable(html));
        QVERIFY(html.contains("x = 1 &lt; 2"));
    }

    void keepsLinks() {
        const QString html = ChatFormat::markdownToHtml("[docs](https://example.org/docs)");
        QVERIFY(html.contains("href=\"https://example.org/docs\""));
    }

    void showsRawHtmlAsText_data() {
        QTest::addColumn<QString>("markdown");
        QTest::addColumn<QString>("forbidden");
        QTest::addRow("span") << "a <b>not bold</b> b" << "<b>";
        QTest::addRow("block") << "<div style=\"color:red\">x</div>" << "color:red\">";
        QTest::addRow("img tag") << "<img src=\"file:///etc/passwd\">" << "<img";
        QTest::addRow("link tag") << "<a href=\"https://evil.example\">x</a>" << "href=\"https://evil";
    }
    void showsRawHtmlAsText() {
        QFETCH(QString, markdown);
        QFETCH(QString, forbidden);
        const QString html = ChatFormat::markdownToHtml(markdown);
        QVERIFY2(!html.contains(forbidden), qPrintable(html));
        QVERIFY(html.contains("&lt;"));   // the markup is still there, as characters
    }

    void replacesImages() {
        const QString html =
            ChatFormat::markdownToHtml("see ![a cat](file:///home/user/cat.png) here");
        QVERIFY2(!html.contains("<img"), qPrintable(html));
        QVERIFY(!html.contains("cat.png"));
        QVERIFY(html.contains("[image"));
        QVERIFY(html.contains("here"));
    }
};

QTEST_MAIN(TstChatFormat)
#include "tst_chatformat.moc"
