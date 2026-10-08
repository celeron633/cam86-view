#include "ui/LogPanel.hpp"
#include <QMenu>
#include <QScrollBar>
namespace cam86::ui {
LogPanel::LogPanel(AppState& state, QWidget* parent) : QPlainTextEdit(parent), state_(state) {
    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(this);
        auto* clearAction = menu.addAction("Clear log");
        if (menu.exec(mapToGlobal(pos)) == clearAction) { state_.log.clear(); refresh(); }
    });
}
void LogPanel::refresh() {
    if (displayed_ == state_.log) return;
    const bool atBottom = verticalScrollBar()->value() >= verticalScrollBar()->maximum();
    const int oldPosition = verticalScrollBar()->value();
    QStringList lines;
    for (const auto& line : state_.log) lines.append(QString::fromStdString(line));
    setPlainText(lines.join('\n'));
    verticalScrollBar()->setValue(atBottom ? verticalScrollBar()->maximum() : oldPosition);
    displayed_ = state_.log;
}
}
