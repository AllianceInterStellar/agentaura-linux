#pragma once

#include <QString>

/// How agent replies are shown. Agents answer in Markdown, so the bubble renders it — but a reply
/// is text somebody else produced, so nothing in it gets to be live HTML: raw HTML is shown as
/// the characters it is, and images are replaced by their description (a bubble that fetched or
/// read files because a reply said so would be a side door).
namespace ChatFormat {

/// Rich text for a QLabel (Qt::RichText) from a Markdown reply.
QString markdownToHtml(const QString &markdown);

}  // namespace ChatFormat
