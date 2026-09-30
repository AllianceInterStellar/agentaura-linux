#include "services/ChatFormat.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QVector>

namespace ChatFormat {

QString markdownToHtml(const QString &markdown) {
    QTextDocument doc;
    doc.setMarkdown(markdown, QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub |
                                                              QTextDocument::MarkdownNoHTML));

    // Swap every image for its alt text. Collected first, replaced back to front, so earlier
    // replacements do not shift the positions still to be visited.
    struct Image { int pos; int len; QString alt; };
    QVector<Image> images;
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            if (!f.isValid() || !f.charFormat().isImageFormat()) continue;
            // QTextFormat::ImageAltText (0x5002), read by value so this compiles on every Qt 6 the
            // app supports; where the Markdown importer does not set it, the alt is just empty.
            const QString alt = f.charFormat().property(0x5002).toString();
            images.push_back({f.position(), f.length(), alt});
        }
    }
    for (auto it = images.crbegin(); it != images.crend(); ++it) {
        QTextCursor c(&doc);
        c.setPosition(it->pos);
        c.setPosition(it->pos + it->len, QTextCursor::KeepAnchor);
        c.insertText(it->alt.isEmpty() ? QStringLiteral("[image]")
                                       : QStringLiteral("[image: %1]").arg(it->alt),
                     QTextCharFormat());
    }
    // The bubbles are dark and QLabel's default link colour is a dark blue that barely shows on
    // them; code blocks get a panel so they read as code. Both set in the document itself, where
    // a stylesheet on the label cannot undo them.
    const QColor linkColor(0x64, 0xB5, 0xF6);
    const QColor codeBackground(0x0A, 0x0A, 0x0B);
    for (QTextBlock b = doc.begin(); b.isValid(); b = b.next()) {
        if (b.blockFormat().nonBreakableLines()) {   // how the importer marks a code block
            QTextCursor c(b);
            QTextBlockFormat bf = b.blockFormat();
            bf.setBackground(codeBackground);
            c.setBlockFormat(bf);
        }
        for (auto it = b.begin(); !it.atEnd(); ++it) {
            const QTextFragment f = it.fragment();
            if (!f.isValid() || !f.charFormat().isAnchor()) continue;
            QTextCursor c(&doc);
            c.setPosition(f.position());
            c.setPosition(f.position() + f.length(), QTextCursor::KeepAnchor);
            QTextCharFormat cf;
            cf.setForeground(linkColor);
            c.mergeCharFormat(cf);
        }
    }
    return doc.toHtml();
}

}  // namespace ChatFormat
