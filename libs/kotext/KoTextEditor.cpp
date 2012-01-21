/* This file is part of the KDE project
 * Copyright (C) 2009 Pierre Stirnweiss <pstirnweiss@googlemail.com>
 * Copyright (C) 2006-2010 Thomas Zander <zander@kde.org>
 * Copyright (c) 2011 Boudewijn Rempt <boud@kogmbh.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "KoTextEditor.h"
#include "KoTextEditor_p.h"

#include "KoDocumentRdfBase.h"
#include "KoBookmark.h"
#include "KoInlineTextObjectManager.h"
#include <KoOdf.h>
#include <KoInlineNote.h>
#include <KoTextPaste.h>
#include <KoShapeController.h>
#include <KoTextOdfSaveHelper.h>
#include "KoTextAnchor.h"
#include "KoTextDocument.h"
#include "KoTextDrag.h"
#include "KoTextLocator.h"
#include "KoTextOdfSaveHelper.h"
#include "KoTableOfContentsGeneratorInfo.h"
#include "KoBibliographyInfo.h"
#include "changetracker/KoChangeTracker.h"
#include "changetracker/KoChangeTrackerElement.h"
#include "changetracker/KoDeleteChangeMarker.h"
#include "styles/KoCharacterStyle.h"
#include "styles/KoParagraphStyle.h"
#include "styles/KoStyleManager.h"
#include "styles/KoTableCellStyle.h"
#include "styles/KoTableColumnStyle.h"
#include "styles/KoTableRowStyle.h"
#include "KoTableColumnAndRowStyleManager.h"
#include "commands/DeleteTableRowCommand.h"
#include "commands/DeleteTableColumnCommand.h"
#include "commands/InsertTableRowCommand.h"
#include "commands/InsertTableColumnCommand.h"
#include "commands/TextPasteCommand.h"
#include "commands/ChangeTrackedDeleteCommand.h"
#include "commands/ListItemNumberingCommand.h"
#include "commands/ChangeListCommand.h"
#include "commands/InsertInlineObjectCommand.h"
#include "commands/DeleteCommand.h"
#include "commands/DeleteAnchorsCommand.h"
#include "KoInlineCite.h"
#include <KoShapeCreateCommand.h>

#include <KLocale>
#include <kundo2stack.h>

#include <QApplication>
#include <QFontDatabase>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextDocumentFragment>
#include <QTextFormat>
#include <QTextTable>
#include <QTextTableCell>
#include <QTimer>
#include <QString>
#include <kundo2command.h>

#include <kdebug.h>

#ifdef SHOULD_BUILD_RDF
#include <rdf/KoDocumentRdf.h>
#else
#include "KoTextSopranoRdfModel_p.h"
#endif

Q_DECLARE_METATYPE(QTextFrame*)

/*Private*/

KoTextEditor::Private::Private(KoTextEditor *qq, QTextDocument *document)
    : q(qq)
    , document (document)
    , headCommand(0)
    , editProtectionCached(false)
{
    caret = QTextCursor(document);
    editorState = NoOp;
}

void KoTextEditor::Private::emitTextFormatChanged()
{
    emit q->textFormatChanged();
}

void KoTextEditor::Private::documentCommandAdded()
{
    class UndoTextCommand : public KUndo2Command
    {
    public:
        UndoTextCommand(QTextDocument *document, KoTextEditor::Private *p, KUndo2Command *parent = 0)
            : KUndo2Command(i18nc("(qtundo-format)", "Text"), parent),
              m_document(document)
            , m_p(p)
        {}

        void undo() {
            QTextDocument *doc = m_document.data();
            if (doc == 0)
                return;
            doc->undo(KoTextDocument(doc).textEditor()->cursor());
            m_p->emitTextFormatChanged();
        }

        void redo() {
            QTextDocument *doc = m_document.data();
            if (doc == 0)
                return;
            doc->redo(KoTextDocument(doc).textEditor()->cursor());
            m_p->emitTextFormatChanged();
        }

        QWeakPointer<QTextDocument> m_document;
        KoTextEditor::Private *m_p;
    };

    //kDebug() << "editor state: " << editorState << " headcommand: " << headCommand;
    if (!headCommand || editorState == NoOp) {
        headCommand = new KUndo2Command(commandTitle);
        if (KoTextDocument(document).undoStack()) {
            //kDebug() << "pushing head: " << headCommand->text();
            KoTextDocument(document).undoStack()->push(headCommand);
        }
    }
    else if ((editorState == KeyPress || editorState == Delete) && headCommand->childCount()) {
        headCommand = new KUndo2Command(commandTitle);
        if (KoTextDocument(document).undoStack()) {
            //kDebug() << "pushing head: " << headCommand->text();
            KoTextDocument(document).undoStack()->push(headCommand);
        }
    }

    new UndoTextCommand(document, this, headCommand);
}

void KoTextEditor::Private::updateState(KoTextEditor::Private::State newState, QString title)
{
    if (editorState == Custom && newState !=NoOp)
        return;
    //kDebug() << "updateState from: " << editorState << " to: " << newState;
    if (editorState != newState || commandTitle != title) {
        if (headCommand /*&& headCommand->childCount() && KoTextDocument(document).undoStack()*/) {
            //kDebug() << "reset headCommand";
            //            KoTextDocument(document).undoStack()->push(headCommand);
            headCommand = 0;
        }
    }
    editorState = newState;
    if (!title.isEmpty())
        commandTitle = title;
    else
        commandTitle = i18n("Text");
    //kDebug() << "commandTitle is now: " << commandTitle;
}

bool KoTextEditor::Private::deleteInlineObjects(bool backwards)
{
    Q_UNUSED(backwards)
    return false;

//    // TODO don't just blindly delete, make this a command so we can undo it later.
//    // Also note that the below code needs unit testing since I found some issues already
//    QTextCursor cursor(*d->caret);

//    KoInlineTextObjectManager *manager = KoTextocument(d->document).inlineObjectTextManager();
//    KoInlineObject *object;
//    bool found = false;

//    if (d->caret->hasSelection()) {
//        QString selected = cursor.selectedText();
//        cursor.setPosition(cursor.selectionStart() + 1);
//        int position = cursor.position();
//        const QChar *data = selected.constData();
//        for (int i = 0; i < selected.length(); i++) {
//            if (data->unicode() == QChar::ObjectReplacementCharacter) {
//                found = true;
//                cursor.setPosition(position);
//                object = manager->inlineTextObject(cursor);

//                if (object)
//                    manager->removeInlineObject(cursor);
//            }
//            // if there is an inline object, the InlineTextObjectManager will also delete the char
//            // so only need to update position if inline object not found
//            else
//                position++;
//            data++;
//        }
//    } else {
//        if (!backward)
//            cursor.movePosition(QTextCursor::Right);
//        object = manager->inlineTextObject(cursor);

//        if (object) {
//            manager->removeInlineObject(cursor);
//            found = true;
//        }
//    }
//    return found;

}

void KoTextEditor::Private::newLine()
{
    if (caret.hasSelection())
        deleteInlineObjects();
    KoTextDocument textDocument(document);
    KoStyleManager *styleManager = textDocument.styleManager();
    KoParagraphStyle *nextStyle = 0;
    KoParagraphStyle *currentStyle = 0;
    if (styleManager) {
        int id = caret.blockFormat().intProperty(KoParagraphStyle::StyleId);
        currentStyle = styleManager->paragraphStyle(id);
        if (currentStyle == 0) // not a style based parag.  Lets make the next one correct.
            nextStyle = styleManager->defaultParagraphStyle();
        else
            nextStyle = styleManager->paragraphStyle(currentStyle->nextStyle());
        Q_ASSERT(nextStyle);
        if (currentStyle == nextStyle)
            nextStyle = 0;
    }

    QTextCharFormat format = caret.charFormat();
    if (format.hasProperty(KoCharacterStyle::ChangeTrackerId)) {
        format.clearProperty(KoCharacterStyle::ChangeTrackerId);
    }

    // Build the block format and subtract the properties that are not inherited
    QTextBlockFormat bf = caret.blockFormat();

    bf.clearProperty(KoParagraphStyle::BreakBefore);
    bf.clearProperty(KoParagraphStyle::ListStartValue);
    bf.clearProperty(KoParagraphStyle::UnnumberedListItem);
    bf.clearProperty(KoParagraphStyle::IsListHeader);
    bf.clearProperty(KoParagraphStyle::MasterPageName);
    bf.clearProperty(KoParagraphStyle::OutlineLevel);

    // Build the block char format which is just a copy
    QTextCharFormat bcf = caret.blockCharFormat();

    // Actually insert the new paragraph char
    int startPosition = caret.position();

    caret.insertBlock(bf, bcf);

    int endPosition = caret.position();

    // Mark the CR as a tracked change
    caret.setPosition(startPosition);
    caret.setPosition(endPosition, QTextCursor::KeepAnchor);

    q->registerTrackedChange(caret, KoGenChange::InsertChange, i18n("Key Press"), format, format, false);

    caret.clearSelection();

    // possibly change the style if requested
    if (nextStyle) {
        QTextBlock block = caret.block();
        if (currentStyle)
            currentStyle->unapplyStyle(block);
        nextStyle->applyStyle(block);
        format = block.charFormat();
    }

    caret.setCharFormat(format);
}

void KoTextEditor::Private::clearCharFormatProperty(int property)
{
    class PropertyWiper : public CharFormatVisitor
    {
    public:
        PropertyWiper(int propertyId) : propertyId(propertyId) {}
        void visit(QTextCharFormat &format) const {
            format.clearProperty(propertyId);
        }

        int propertyId;
    };
    PropertyWiper wiper(property);
    CharFormatVisitor::visitSelection(q, wiper,QString(), false);
}

/*KoTextEditor*/

//TODO factor out the changeTracking charFormat setting from all individual slots to a public slot, which will be available for external commands (TextShape)

//The BlockFormatVisitor and CharFormatVisitor are used when a property needs to be modified relative to its current value (which could be different over the selection). For example: increase indentation by 10pt.
//The BlockFormatVisitor is also used for the change tracking of a blockFormat. The changeTracker stores the information about the changeId in the charFormat. The BlockFormatVisitor ensures that thd changeId is set on the whole block (even if only a part of the block is actually selected).
//Should such mechanisms be later provided directly by Qt, we could dispose of these classes.


KoTextEditor::KoTextEditor(QTextDocument *document)
    : QObject(document),
      d (new Private(this, document))
{
    connect (d->document, SIGNAL (undoCommandAdded()), this, SLOT (documentCommandAdded()));
}

KoTextEditor::~KoTextEditor()
{
    delete d;
}

KoTextEditor *KoTextEditor::getTextEditorFromCanvas(KoCanvasBase *canvas)
{
    KoSelection *selection = canvas->shapeManager()->selection();
    if (selection) {
        foreach(KoShape *shape, selection->selectedShapes()) {
            if (KoTextShapeDataBase *textData = qobject_cast<KoTextShapeDataBase*>(shape->userData())) {
                KoTextDocument doc(textData->document());
                return doc.textEditor();
            }
        }
    }
    return 0;
}

QTextCursor* KoTextEditor::cursor()
{
    return &(d->caret);
}

void KoTextEditor::addCommand(KUndo2Command *command)
{
    d->updateState(KoTextEditor::Private::Custom, (!command->text().isEmpty())?command->text():i18n("Text"));
    //kDebug() << "will push the custom command: " << command->text();
    d->headCommand = command;
    KUndo2QStack *stack = KoTextDocument(d->document).undoStack();
    if (stack) {
        stack->push(command);
    }
    //kDebug() << "custom command pushed";
}

void KoTextEditor::instantlyExecuteCommand(KUndo2Command *command)
{
    d->updateState(KoTextEditor::Private::Custom, (!command->text().isEmpty())?command->text():i18n("Text"));
    //kDebug() << "will push the custom command: " << command->text();
    d->headCommand = command; // So any text it does is store as sub commands
    command->redo();
    // instant replay done let's not keep it dangling
    d->updateState(KoTextEditor::Private::NoOp);
}

void KoTextEditor::registerTrackedChange(QTextCursor &selection, KoGenChange::Type changeType, QString title, QTextFormat& format, QTextFormat& prevFormat, bool applyToWholeBlock)
{
    if (!KoTextDocument(d->document).changeTracker() || !KoTextDocument(d->document).changeTracker()->recordChanges()) {
        // clear the ChangeTrackerId from the passed in selection, without recursively registring
        // change tracking again  ;)
        int start = qMin(selection.position(), selection.anchor());
        int end = qMax(selection.position(), selection.anchor());

        QTextBlock block = selection.block();
        if (block.position() > start)
            block = block.document()->findBlock(start);

        while (block.isValid() && block.position() < end) {
            QTextBlock::iterator iter = block.begin();
            while (!iter.atEnd()) {
                QTextFragment fragment = iter.fragment();
                if (fragment.position() > end) {
                    break;
                }

                if (fragment.position() + fragment.length() <= start) {
                    ++iter;
                    continue;
                }

                QTextCursor cursor(block);
                cursor.setPosition(fragment.position());
                QTextCharFormat fm = fragment.charFormat();

                if (fm.hasProperty(KoCharacterStyle::ChangeTrackerId)) {
                    fm.clearProperty(KoCharacterStyle::ChangeTrackerId);
                    int to = qMin(end, fragment.position() + fragment.length());
                    cursor.setPosition(to, QTextCursor::KeepAnchor);
                    cursor.setCharFormat(fm);
                    iter = block.begin();
                } else {
                    ++iter;
                }
            }
            block = block.next();
        }
    } else {
        if (changeType != KoGenChange::DeleteChange) {
            //first check if there already is an identical change registered just before or just after the selection. If so, merge appropriatly.
            //TODO implement for format change. handle the prevFormat/newFormat check.
            QTextCursor checker = QTextCursor(selection);
            int idBefore = 0;
            int idAfter = 0;
            int changeId = 0;
            int selectionBegin = qMin(checker.anchor(), checker.position());
            int selectionEnd = qMax(checker.anchor(), checker.position());

            checker.setPosition(selectionBegin);
            KoChangeTracker *changeTracker = KoTextDocument(d->document).changeTracker();
            if (!checker.atBlockStart()) {
                int changeId = checker.charFormat().property(KoCharacterStyle::ChangeTrackerId).toInt();
                if (changeId && changeTracker->elementById(changeId)->getChangeType() == changeType)
                    idBefore = changeId;
            } else {
                if (!checker.currentTable()) {
                    int changeId = checker.blockFormat().intProperty(KoCharacterStyle::ChangeTrackerId);
                    if (changeId && changeTracker->elementById(changeId)->getChangeType() == changeType)
                        idBefore = changeId;
                } else {
                    idBefore = checker.currentTable()->format().intProperty(KoCharacterStyle::ChangeTrackerId);
                    if (!idBefore) {
                        idBefore = checker.currentTable()->cellAt(checker).format().intProperty(KoCharacterStyle::ChangeTrackerId);
                    }
                }
            }

            checker.setPosition(selectionEnd);
            if (!checker.atEnd()) {
                checker.movePosition(QTextCursor::NextCharacter);
                idAfter = KoTextDocument(d->document).changeTracker()->mergeableId(changeType, title, checker.charFormat().property( KoCharacterStyle::ChangeTrackerId ).toInt());
            }
            changeId = (idBefore)?idBefore:idAfter;

            switch (changeType) {//TODO: this whole thing actually needs to be done like a visitor. If the selection contains several change regions, the parenting needs to be individualised.
            case KoGenChange::InsertChange:
                if (!changeId)
                    changeId = KoTextDocument(d->document).changeTracker()->getInsertChangeId(title, 0);
                break;
            case KoGenChange::FormatChange:
                if (!changeId)
                    changeId = KoTextDocument(d->document).changeTracker()->getFormatChangeId(title, format, prevFormat, 0);
                break;
            case KoGenChange::DeleteChange:
                //this should never be the case
                break;
            default:
                ;// do nothing
            }

            if (applyToWholeBlock) {
                selection.movePosition(QTextCursor::StartOfBlock);
                selection.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
            }

            QTextCharFormat f;
            f.setProperty(KoCharacterStyle::ChangeTrackerId, changeId);
            selection.mergeCharFormat(f);

            QTextBlock startBlock = selection.document()->findBlock(selection.anchor());
            QTextBlock endBlock = selection.document()->findBlock(selection.position());

            while (startBlock.isValid() && startBlock != endBlock) {
                startBlock = startBlock.next();
                QTextCursor cursor(startBlock);
                QTextBlockFormat blockFormat;
                blockFormat.setProperty(KoCharacterStyle::ChangeTrackerId, changeId);
                cursor.mergeBlockFormat(blockFormat);

                QTextCharFormat blockCharFormat = cursor.blockCharFormat();
                if (blockCharFormat.hasProperty(KoCharacterStyle::ChangeTrackerId)) {
                    blockCharFormat.clearProperty(KoCharacterStyle::ChangeTrackerId);
                    cursor.setBlockCharFormat(blockCharFormat);
                }
            }
        }
    }
}

void KoTextEditor::bold(bool bold)
{
    if (isEditProtected()) {
        return;
    }

    d->updateState(KoTextEditor::Private::Format, i18n("Bold"));
    QTextCharFormat format;
    format.setFontWeight(bold ? QFont::Bold : QFont::Normal);

    QTextCharFormat prevFormat(d->caret.charFormat());
    d->caret.mergeCharFormat(format);
    registerTrackedChange(d->caret, KoGenChange::FormatChange, i18n("Bold"), format, prevFormat, false);
    d->updateState(KoTextEditor::Private::NoOp);
    emit textFormatChanged();
}

void KoTextEditor::italic(bool italic)
{
    if (isEditProtected()) {
        return;
    }

    d->updateState(KoTextEditor::Private::Format, i18n("Italic"));
    QTextCharFormat format;
    format.setFontItalic(italic);

    QTextCharFormat prevFormat(d->caret.charFormat());
    d->caret.mergeCharFormat(format);
    registerTrackedChange(d->caret, KoGenChange::FormatChange, i18n("Italic"), format, prevFormat, false);
    d->updateState(KoTextEditor::Private::NoOp);
    emit textFormatChanged();
}

void KoTextEditor::underline(bool underline)
{
    if (isEditProtected()) {
        return;
    }

    d->updateState(KoTextEditor::Private::Format, i18n("Underline"));
    QTextCharFormat format;
    if (underline) {
        format.setProperty(KoCharacterStyle::UnderlineType, KoCharacterStyle::SingleLine);
        format.setProperty(KoCharacterStyle::UnderlineStyle, KoCharacterStyle::SolidLine);
    } else {
        format.setProperty(KoCharacterStyle::UnderlineType, KoCharacterStyle::NoLineType);
        format.setProperty(KoCharacterStyle::UnderlineStyle, KoCharacterStyle::NoLineStyle);
    }

    QTextCharFormat prevFormat(d->caret.charFormat());
    d->caret.mergeCharFormat(format);
    registerTrackedChange(d->caret, KoGenChange::FormatChange, i18n("Underline"), format, prevFormat, false);
    d->updateState(KoTextEditor::Private::NoOp);
    emit textFormatChanged();
}

void KoTextEditor::strikeOut(bool strikeout)
{
    if (isEditProtected()) {
        return;
    }

    d->updateState(KoTextEditor::Private::Format, i18n("Strike Out"));
    QTextCharFormat format;
    if (strikeout) {
        format.setProperty(KoCharacterStyle::StrikeOutType, KoCharacterStyle::SingleLine);
        format.setProperty(KoCharacterStyle::StrikeOutStyle, KoCharacterStyle::SolidLine);
    } else {
        format.setProperty(KoCharacterStyle::StrikeOutType, KoCharacterStyle::NoLineType);
        format.setProperty(KoCharacterStyle::StrikeOutStyle, KoCharacterStyle::NoLineStyle);
    }
    QTextCharFormat prevFormat(d->caret.charFormat());
    d->caret.mergeCharFormat(format);
    registerTrackedChange(d->caret, KoGenChange::FormatChange, i18n("Strike Out"), format, prevFormat, false);
    d->updateState(KoTextEditor::Private::NoOp);
    emit textFormatChanged();
}

void KoTextEditor::setHorizontalTextAlignment(Qt::Alignment align)
{
    if (isEditProtected()) {
        return;
    }

    class Aligner : public BlockFormatVisitor
    {
    public:
        Aligner(Qt::Alignment align) : alignment(align) {}
        void visit(QTextBlockFormat &format) const {
            format.setAlignment(alignment);
        }
        Qt::Alignment alignment;
    };

    Aligner aligner(align);
    d->updateState(KoTextEditor::Private::Format, i18n("Set Horizontal Alignment"));
    BlockFormatVisitor::visitSelection(this, aligner, i18n("Set Horizontal Alignment"));
    d->updateState(KoTextEditor::Private::NoOp);
    emit textFormatChanged();
}

void KoTextEditor::setVerticalTextAlignment(Qt::Alignment align)
{
    if (isEditProtected()) {
        return;
    }

    QTextCharFormat::VerticalAlignment charAlign = QTextCharFormat::AlignNormal;
    if (align == Qt::AlignTop)
        charAlign = QTextCharFormat::AlignSuperScript;
    else if (align == Qt::AlignBottom)
        charAlign = QTextCharFormat::AlignSubScript;

    d->updateState(KoTextEditor::Private::Format, i18n("Set Vertical Alignment"));
    QTextCharFormat format;
    format.setVerticalAlignment(charAlign);
    QTextCharFormat prevFormat(d->caret.charFormat());
    d->caret.mergeCharFormat(format);
    registerTrackedChange(d->caret, KoGenChange::FormatChange, i18n("Set Vertical Alignment"), format, prevFormat, false);
    d->updateState(KoTextEditor::Private::NoOp);
    emit textFormatChanged();
}

void KoTextEditor::decreaseIndent()
{
    if (isEditProtected()) {
        return;
    }

    class Indenter : public BlockFormatVisitor
    {
    public:
        void visit(QTextBlockFormat &format) const {
            // TODO make the 10 configurable.
            format.setLeftMargin(qMax(qreal(0.0), format.leftMargin() - 10));
        }
        Qt::Alignment alignment;
    };

    Indenter indenter;
    d->updateState(KoTextEditor::Private::Format, i18n("Decrease Indent"));
    BlockFormatVisitor::visitSelection(this, indenter, i18n("Decrease Indent"));
    d->updateState(KoTextEditor::Private::NoOp);
    emit textFormatChanged();
}

void KoTextEditor::increaseIndent()
{
    if (isEditProtected()) {
        return;
    }

    class Indenter : public BlockFormatVisitor
    {
    public:
        void visit(QTextBlockFormat &format) const {
            // TODO make the 10 configurable.
            format.setLeftMargin(format.leftMargin() + 10);
        }
        Qt::Alignment alignment;
    };

    Indenter indenter;
    d->updateState(KoTextEditor::Private::Format, i18n("Increase Indent"));
    BlockFormatVisitor::visitSelection(this, indenter, i18n("Increase Indent"));
    d->updateState(KoTextEditor::Private::NoOp);
    emit textFormatChanged();
}

class FontResizer : public CharFormatVisitor
{
public:
    enum Type { Grow, Shrink };
    FontResizer(Type type_) : type(type_) {
        QFontDatabase fontDB;
        defaultSizes = fontDB.standardSizes();
    }
    void visit(QTextCharFormat &format) const {
        const qreal current = format.fontPointSize();
        int prev = 1;
        foreach(int pt, defaultSizes) {
            if ((type == Grow && pt > current) || (type == Shrink && pt >= current)) {
                format.setFontPointSize(type == Grow ? pt : prev);
                return;
            }
            prev = pt;
        }
    }

    QList<int> defaultSizes;
    const Type type;
};

void KoTextEditor::decreaseFontSize()
{
    if (isEditProtected()) {
        return;
    }

    d->updateState(KoTextEditor::Private::Format, i18n("Decrease font size"));
    FontResizer sizer(FontResizer::Shrink);
    CharFormatVisitor::visitSelection(this, sizer, i18n("Decrease font size"));
    d->updateState(KoTextEditor::Private::NoOp);
    emit textFormatChanged();
}

void KoTextEditor::increaseFontSize()
{
    if (isEditProtected()) {
        return;
    }

    d->updateState(KoTextEditor::Private::Format, i18n("Increase font size"));
    FontResizer sizer(FontResizer::Grow);
    CharFormatVisitor::visitSelection(this, sizer, i18n("Increase font size"));
    d->updateState(KoTextEditor::Private::NoOp);
    emit textFormatChanged();
}

void KoTextEditor::setFontFamily(const QString &font)
{
    if (isEditProtected()) {
        return;
    }

    d->updateState(KoTextEditor::Private::Format, i18n("Set Font"));
    QTextCharFormat format;
    format.setFontFamily(font);
    QTextCharFormat prevFormat(d->caret.charFormat());
    d->caret.mergeCharFormat(format);
    registerTrackedChange(d->caret, KoGenChange::FormatChange, i18n("Set Font"), format, prevFormat, false);
    d->updateState(KoTextEditor::Private::NoOp);
    emit textFormatChanged();
}

void KoTextEditor::setFontSize(qreal size)
{
    if (isEditProtected()) {
        return;
    }

    d->updateState(KoTextEditor::Private::Format, i18n("Set Font Size"));
    QTextCharFormat format;
    format.setFontPointSize(size);
    QTextCharFormat prevFormat(d->caret.charFormat());
    d->caret.mergeCharFormat(format);
    registerTrackedChange(d->caret, KoGenChange::FormatChange, i18n("Set Font Size"), format, prevFormat, false);
    d->updateState(KoTextEditor::Private::NoOp);
    emit textFormatChanged();
}

void KoTextEditor::setTextBackgroundColor(const QColor &color)
{
    if (isEditProtected()) {
        return;
    }

    d->updateState(KoTextEditor::Private::Format, i18n("Set Background Color"));
    QTextCharFormat format;
    format.setBackground(QBrush(color));
    QTextCharFormat prevFormat(d->caret.charFormat());
    d->caret.mergeCharFormat(format);
    registerTrackedChange(d->caret, KoGenChange::FormatChange, i18n("Set Background Color"), format, prevFormat, false);
    d->updateState(KoTextEditor::Private::NoOp);
    emit textFormatChanged();
}

void KoTextEditor::setTextColor(const QColor &color)
{
    if (isEditProtected()) {
        return;
    }

    d->updateState(KoTextEditor::Private::Format, i18n("Set Text Color"));
    QTextCharFormat format;
    format.setForeground(QBrush(color));
    QTextCharFormat prevFormat(d->caret.charFormat());
    d->caret.mergeCharFormat(format);
    registerTrackedChange(d->caret, KoGenChange::FormatChange, i18n("Set Text Color"), format, prevFormat, false);
    d->updateState(KoTextEditor::Private::NoOp);
    emit textFormatChanged();
}

class KoTextVisitor
{
public:
    KoTextVisitor(KoTextEditor *editor)
        : m_abortVisiting(false)
        , m_editor(editor)
    {
    }

    virtual ~KoTextVisitor() {}
    // called whenever a visit was prevented by editprotection
    virtual void nonVisit() {}

    virtual void visitFragmentSelection(QTextCursor )
    {
    }

    // The default implementation calls visitFragmentSelection on each fragment.intersect.selection
    virtual void visitBlock(QTextBlock block, const QTextCursor &caret)
    {
        for (QTextBlock::iterator it = block.begin(); it != block.end(); ++it) {
            QTextCursor fragmentSelection;
            fragmentSelection.setPosition(qMax(caret.selectionStart(), it.fragment().position()));
            fragmentSelection.setPosition(qMin(caret.selectionEnd(), it.fragment().position() + it.fragment().length()), QTextCursor::KeepAnchor);

            if (fragmentSelection.anchor() >= fragmentSelection.position()) {
                continue;
            }
            visitFragmentSelection(fragmentSelection);
        }
    }

    bool abortVisiting() { return m_abortVisiting;}
    void setAbortVisiting(bool abort) {m_abortVisiting = abort;}
    KoTextEditor * editor() {return m_editor;}
private:
    bool m_abortVisiting;
    KoTextEditor *m_editor;
};

class SetCharacterStyleVisitor : public KoTextVisitor
{
public:
    SetCharacterStyleVisitor(KoTextEditor *editor, KoCharacterStyle *style)
        : KoTextVisitor(editor)
        , m_style(style)
    {
    }

    virtual void visitBlock(QTextBlock block, const QTextCursor &caret)
    {
        m_newFormat = block.charFormat();
        m_style->applyStyle(m_newFormat);
        m_style->ensureMinimalProperties(m_newFormat);

        KoTextVisitor::visitBlock(block, caret);


        QList<QTextCharFormat>::Iterator it = m_formats.begin();
        foreach(QTextCursor cursor, m_cursors) {
            cursor.setCharFormat(*it);
            QTextFormat prevFormat(cursor.charFormat());
            editor()->registerTrackedChange(cursor, KoGenChange::FormatChange, i18n("Set Character Style"), *it, prevFormat, false);
            ++it;
        }
    }

    virtual void visitFragmentSelection(QTextCursor fragmentSelection)
    {
        QTextCharFormat format = m_newFormat;
        fragmentSelection.charFormat();

        QVariant v;
        v = fragmentSelection.charFormat().property(KoCharacterStyle::InlineInstanceId);
        if (!v.isNull()) {
            format.setProperty(KoCharacterStyle::InlineInstanceId, v);
        }

        v = fragmentSelection.charFormat().property(KoCharacterStyle::ChangeTrackerId);
        if (!v.isNull()) {
            format.setProperty(KoCharacterStyle::ChangeTrackerId, v);
        }

        if (fragmentSelection.charFormat().isAnchor()) {
            format.setAnchor(true);
            format.setAnchorHref(fragmentSelection.charFormat().anchorHref());
        }
        m_formats.append(format);
        m_cursors.append(fragmentSelection);
    }

    KoCharacterStyle *m_style;
    QTextCharFormat m_newFormat;
    QList<QTextCharFormat> m_formats;
    QList<QTextCursor> m_cursors;
};

void KoTextEditor::setStyle(KoCharacterStyle *style)
{
    Q_ASSERT(style);
    d->updateState(KoTextEditor::Private::Format, i18n("Set Character Style"));

    SetCharacterStyleVisitor visitor(this, style);

    recursivelyVisitSelection(d->document->rootFrame()->begin(), visitor);
    d->updateState(KoTextEditor::Private::NoOp);
    emit textFormatChanged();
}

// To figure out if a the blocks of the selection are write protected we need to
// traverse the entire document as sections build up the protectiveness recursively.
void KoTextEditor::recursivelyVisitSelection(QTextFrame::iterator it, KoTextVisitor &visitor) const
{
    do {
        if (visitor.abortVisiting())
            return;

        QTextBlock block = it.currentBlock();
        QTextTable *table = qobject_cast<QTextTable*>(it.currentFrame());
        QTextFrame *subFrame = it.currentFrame();
        if (table) {
            // There are 4 ways this table can be selected:
            //  - "before to mid"
            //  - "mid to after"
            //  - "complex mid to mid"
            //  - "simple mid to mid"
            // The 3 first are entire cells, the fourth is within a cell

            if (d->caret.selectionStart() <= table->lastPosition()
                    && d->caret.selectionEnd() >= table->firstPosition()) {
                // We have a selection somewhere
                QTextTableCell cell1 = table->cellAt(d->caret.selectionStart());
                QTextTableCell cell2 = table->cellAt(d->caret.selectionEnd());
                if (cell1 != cell2 || !cell1.isValid() || !cell2.isValid()) {
                    // And the selection is complex or entire table
                    int selectionRow;
                    int selectionColumn;
                    int selectionRowSpan;
                    int selectionColumnSpan;
                    if (!cell1.isValid() || !cell2.isValid()) {
                        // entire table
                        selectionRow = selectionColumn = 0;
                        selectionRowSpan = table->rows();
                        selectionColumnSpan = table->columns();
                    } else {
                        d->caret.selectedTableCells(&selectionRow, &selectionRowSpan, &selectionColumn, &selectionColumnSpan);
                    }

                    for (int r = selectionRow; r < selectionRow + selectionRowSpan; r++) {
                        for (int c = selectionColumn; c < selectionColumn +
                             selectionColumnSpan; c++) {
                            QTextTableCell cell = table->cellAt(r,c);
                            if (!cell.format().boolProperty(KoTableCellStyle::CellIsProtected)) {
                                recursivelyVisitSelection(cell.begin(), visitor);
                            } else {
                                visitor.nonVisit();
                            }

                            if (visitor.abortVisiting())
                                return;
                        }
                    }
                } else {
                    // And the selection is simple
                    if (!cell1.format().boolProperty(KoTableCellStyle::CellIsProtected)) {
                        recursivelyVisitSelection(cell1.begin(), visitor);
                    } else {
                        visitor.nonVisit();
                    }
                    return;
                }
            }
            if (d->caret.selectionEnd() <= table->lastPosition()) {
                return;
            }
        } if (subFrame) {
        } else {
            // TODO build up the section stack

            if (d->caret.selectionStart() < block.position() + block.length()
                    && d->caret.selectionEnd() >= block.position()) {
                // We have a selection somewhere
                if (true) { // TODO don't change if block is protected by section
                    visitor.visitBlock(block, d->caret);
                } else {
                    visitor.nonVisit();
                }
            }

            // TODO tear down the section stack

            if (d->caret.selectionEnd() < block.position() + block.length()) {
                return;
            }
        }
        if (!it.atEnd()) {
            ++it;
        }
    } while (!it.atEnd());
}

class SetParagraphStyleVisitor : public KoTextVisitor
{
public:
    SetParagraphStyleVisitor(KoTextEditor *editor, KoStyleManager *styleManager, KoParagraphStyle *style)
        : KoTextVisitor(editor)
        , m_styleManager(styleManager)
        , m_style(style)
    {
    }

    virtual void visitBlock(QTextBlock block, const QTextCursor &)
    {
        if (m_styleManager) {
            QTextBlockFormat bf = block.blockFormat();
            KoParagraphStyle *old = m_styleManager->paragraphStyle(bf.intProperty(KoParagraphStyle::StyleId));
            if (old)
                old->unapplyStyle(block);
        }
        // above should unaaply the style and it's lists part, but we want to clear everything
        QTextCursor cursor(block);
        cursor.setBlockFormat(QTextBlockFormat());
        m_style->applyStyle(block);
    }

    KoStyleManager *m_styleManager;
    KoParagraphStyle *m_style;
};

void KoTextEditor::setStyle(KoParagraphStyle *style)
{
    d->updateState(KoTextEditor::Private::Format, i18n("Set Paragraph Style"));

    KoStyleManager *styleManager = KoTextDocument(d->document).styleManager();
    SetParagraphStyleVisitor visitor(this, styleManager, style);

    recursivelyVisitSelection(d->document->rootFrame()->begin(), visitor);

    d->updateState(KoTextEditor::Private::NoOp);
    emit textFormatChanged();
}

void KoTextEditor::addBookmark(const QString &name)
{//TODO changeTracking
    if (isEditProtected()) {
        return;
    }

    d->updateState(KoTextEditor::Private::Custom, i18n("Insert Bookmark"));
    KoBookmark *bookmark = new KoBookmark(d->document);
    bookmark->setName(name);
    int startPos = -1, endPos = -1, caretPos = -1;

    if (d->caret.hasSelection()) {
        startPos = d->caret.selectionStart();
        endPos = d->caret.selectionEnd();
        caretPos = d->caret.position();

        d->caret.setPosition(endPos);
        KoBookmark *endBookmark = new KoBookmark(d->document);
        endBookmark->setName(name);
        bookmark->setType(KoBookmark::StartBookmark);
        endBookmark->setType(KoBookmark::EndBookmark);
        KoTextDocument(d->document).inlineTextObjectManager()->insertInlineObject(d->caret, endBookmark);
        bookmark->setEndBookmark(endBookmark);
        d->caret.setPosition(startPos);
    } else {
        bookmark->setType(KoBookmark::SinglePosition);
    }
    // TODO the macro & undo things
    KoTextDocument(d->document).inlineTextObjectManager()->insertInlineObject(d->caret, bookmark);
    if (startPos != -1) {
        // TODO repaint selection properly
        if (caretPos == startPos) {
            startPos = endPos + 1;
            endPos = caretPos;
        } else {
            endPos += 2;
        }
        d->caret.setPosition(startPos);
        d->caret.setPosition(endPos, QTextCursor::KeepAnchor);
    }
    d->updateState(KoTextEditor::Private::NoOp);
}

KoInlineObject *KoTextEditor::insertIndexMarker()
{//TODO changeTracking
    if (isEditProtected()) {
        return 0;
    }

    int startPosition = d->caret.position();

    if (d->caret.blockFormat().hasProperty(KoParagraphStyle::HiddenByTable)) {
        d->caret.movePosition(QTextCursor::PreviousCharacter);
        if (startPosition == d->caret.position()) {
            d->newLine();
            d->caret.movePosition(QTextCursor::PreviousCharacter);
        } else {
            d->newLine();
        }
        QTextBlockFormat bf = d->caret.blockFormat();
        bf.clearProperty(KoParagraphStyle::HiddenByTable);
        d->caret.setBlockFormat(bf);

        startPosition = d->caret.position();
    }

    QTextBlock block = d->caret.block();
    if (d->caret.position() >= block.position() + block.length() - 1)
        return 0; // can't insert one at end of text
    if (block.text()[ d->caret.position() - block.position()].isSpace())
        return 0; // can't insert one on a whitespace as that does not indicate a word.

    d->updateState(KoTextEditor::Private::Custom, i18n("Insert Index"));
    KoTextLocator *tl = new KoTextLocator();
    KoTextDocument(d->document).inlineTextObjectManager()->insertInlineObject(d->caret, tl);
    d->updateState(KoTextEditor::Private::NoOp);
    return tl;
}

void KoTextEditor::insertInlineObject(KoInlineObject *inliner, KUndo2Command *cmd)
{
    if (isEditProtected()) {
        return;
    }

    d->updateState(KoTextEditor::Private::Custom, i18n("Insert Variable"));

    int startPosition = d->caret.position();

    if (d->caret.blockFormat().hasProperty(KoParagraphStyle::HiddenByTable)) {
        d->caret.movePosition(QTextCursor::PreviousCharacter);
        if (startPosition == d->caret.position()) {
            d->newLine();
            d->caret.movePosition(QTextCursor::PreviousCharacter);
        } else {
            d->newLine();
        }
        QTextBlockFormat bf = d->caret.blockFormat();
        bf.clearProperty(KoParagraphStyle::HiddenByTable);
        d->caret.setBlockFormat(bf);

        startPosition = d->caret.position();
    }

    QTextCharFormat format = d->caret.charFormat();
    if (format.hasProperty(KoCharacterStyle::ChangeTrackerId)) {
        format.clearProperty(KoCharacterStyle::ChangeTrackerId);
    }


    int endPosition = d->caret.position();
    d->caret.setPosition(startPosition);
    d->caret.setPosition(endPosition, QTextCursor::KeepAnchor);
    registerTrackedChange(d->caret, KoGenChange::InsertChange, i18n("Key Press"), format, format, false);
    d->caret.clearSelection();

    InsertInlineObjectCommand *insertInlineObjectCommand = new InsertInlineObjectCommand(inliner, d->document, cmd);

    if (!cmd) {
        addCommand(insertInlineObjectCommand);
    }

    d->updateState(KoTextEditor::Private::NoOp);

    emit cursorPositionChanged();
}

void KoTextEditor::updateInlineObjectPosition(int start, int end)
{
    KoInlineTextObjectManager *inlineObjectManager = KoTextDocument(d->document).inlineTextObjectManager();
    // and, of course, every inline object after the current position has the wrong position
    QTextCursor cursor = d->document->find(QString(QChar::ObjectReplacementCharacter), start);
    while (!cursor.isNull() && (end > -1 && cursor.position() < end )) {
        QTextCharFormat fmt = cursor.charFormat();
        KoInlineObject *obj = inlineObjectManager->inlineTextObject(fmt);
        obj->updatePosition(d->document, cursor.position(), fmt);
        cursor = d->document->find(QString(QChar::ObjectReplacementCharacter), cursor.position());
    }

}

void KoTextEditor::removeAnchors(const QList<KoTextAnchor*> &anchors, KUndo2Command *parent)
{
    Q_ASSERT(parent);
    instantlyExecuteCommand(new DeleteAnchorsCommand(anchors, d->document, parent));
}

void KoTextEditor::insertFrameBreak()
{
    if (isEditProtected()) {
        return;
    }

    d->updateState(KoTextEditor::Private::KeyPress, i18n("Insert Break"));
    QTextBlock block = d->caret.block();
    if (d->caret.position() == block.position() && block.length() > 0) { // start of parag
        QTextBlockFormat bf = d->caret.blockFormat();
        bf.setProperty(KoParagraphStyle::BreakBefore, KoText::PageBreak);
        d->caret.insertBlock(bf);
        if (block.textList())
            block.textList()->remove(block);
    } else {
        QTextBlockFormat bf = d->caret.blockFormat();
        newLine();
        bf = d->caret.blockFormat();
        bf.setProperty(KoParagraphStyle::BreakBefore, KoText::PageBreak);
        d->caret.setBlockFormat(bf);
    }
    d->updateState(KoTextEditor::Private::NoOp);
    emit cursorPositionChanged();
}

bool KoTextEditor::deleteInlineObjects(bool backward)
{
    if (isEditProtected()) {
        return false;
    }

    return d->deleteInlineObjects(backward);
}

void KoTextEditor::paste(const QMimeData *mimeData,
                         KoShapeController *shapeController,
                         bool pasteAsText)
{
    if (isEditProtected()) {
        return;
    }

    addCommand(new TextPasteCommand(mimeData,
                                    d->document,
                                    shapeController,
                                    0,
                                    pasteAsText));
}

bool KoTextEditor::paste(KoTextEditor *editor,
                         KoShapeController *shapeController,
                         bool pasteAsText)
{

    Q_ASSERT(editor);
    Q_ASSERT(editor != this);

    if (!editor->hasSelection()) {
        editor->setPosition(0);
        editor->movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    }

    int from = editor->position();
    int to = editor->anchor();
    KoTextOdfSaveHelper saveHelper(editor->document(), from, to);
    KoTextDrag drag;

    KoDocumentRdfBase *rdf = 0;
    if (shapeController->resourceManager()->hasResource(KoText::DocumentRdf)) {
        rdf = static_cast<KoDocumentRdfBase*>(shapeController->resourceManager()->resource(KoText::DocumentRdf).value<void*>());
        saveHelper.setRdfModel(rdf->model());
    }

    drag.setOdf(KoOdf::mimeType(KoOdf::Text), saveHelper);

    beginEditBlock();

    if (hasSelection()) {
        addCommand(new DeleteCommand(DeleteCommand::NextChar, d->document, shapeController));
    }

    // check for mime type
    const QMimeData *data = drag.mimeData();

    if (data->hasFormat(KoOdf::mimeType(KoOdf::Text))
                    || data->hasFormat(KoOdf::mimeType(KoOdf::OpenOfficeClipboard)) ) {
        KoOdf::DocumentType odfType = KoOdf::Text;
        if (!data->hasFormat(KoOdf::mimeType(odfType))) {
            odfType = KoOdf::OpenOfficeClipboard;
        }

        if (pasteAsText) {
            insertText(data->text());
        } else {
            const Soprano::Model *rdfModel = 0;
#ifdef SHOULD_BUILD_RDF
            bool weOwnRdfModel = true;
            rdfModel = Soprano::createModel();
            if (rdf) {
                delete rdfModel;
                rdfModel = rdf->model();
                weOwnRdfModel = false;
            }
#endif

            //kDebug() << "pasting odf text";
            KoTextPaste paste(this, shapeController, rdfModel);
            paste.paste(odfType, data);
            //kDebug() << "done with pasting odf";

#ifdef SHOULD_BUILD_RDF
            if (rdf) {
                rdf->updateInlineRdfStatements(d->document);
            }
            if (weOwnRdfModel && rdfModel) {
                delete rdfModel;
            }
#endif
        }
    }

    endEditBlock();

    return true;
}

void KoTextEditor::deleteChar(MoveOperation direction, bool trackChanges, KoShapeController *shapeController)
{
    if (isEditProtected()) {
        return;
    }

    if (direction == PreviousChar) {
        if (d->caret.block().blockFormat().hasProperty(KoParagraphStyle::HiddenByTable)) {
            movePosition(QTextCursor::PreviousCharacter);
            return; // it becomes just a cursor movement;
        }
    } else {
        QTextCursor tmpCursor = d->caret;
        tmpCursor.movePosition(QTextCursor::NextCharacter);
        if (tmpCursor.block().blockFormat().hasProperty(KoParagraphStyle::HiddenByTable)) {
            movePosition(QTextCursor::NextCharacter);
            return; // it becomes just a cursor movement;
        }

    }

    if (trackChanges) {
        if (direction == PreviousChar) {
            addCommand(new ChangeTrackedDeleteCommand(ChangeTrackedDeleteCommand::PreviousChar,
                                                      d->document,
                                                      shapeController));
        }
        else {
            addCommand(new ChangeTrackedDeleteCommand(ChangeTrackedDeleteCommand::NextChar,
                                                      d->document,
                                                      shapeController));
        }
    }
    else {
        if (direction == PreviousChar) {
            addCommand(new DeleteCommand(DeleteCommand::PreviousChar,
                                         d->document,
                                         shapeController));
        }
        else {
            addCommand(new DeleteCommand(DeleteCommand::NextChar,
                                         d->document,
                                         shapeController));
        }
    }
}

void KoTextEditor::toggleListNumbering(bool numberingEnabled)
{
    if (isEditProtected()) {
        return;
    }

    addCommand(new ListItemNumberingCommand(block(), numberingEnabled));
    emit textFormatChanged();
}

void KoTextEditor::setListProperties(KoListStyle::Style style,
                                     int level,
                                     ChangeListFlags flags)
{
    if (isEditProtected()) {
        return;
    }

    if (flags & AutoListStyle && d->caret.block().textList() == 0) {
        flags = MergeWithAdjacentList;
    }
    addCommand(new ChangeListCommand(d->caret, style, level, flags));
    emit textFormatChanged();
}


int KoTextEditor::anchor() const
{
    return d->caret.anchor();
}

bool KoTextEditor::atBlockEnd() const
{
    return d->caret.atBlockEnd();
}

bool KoTextEditor::atBlockStart() const
{
    return d->caret.atBlockStart();
}

bool KoTextEditor::atEnd() const
{
    QVariant resource = d->caret.document()->resource(KoTextDocument::AuxillaryFrame,
    KoTextDocument::AuxillaryFrameURL);
    QTextFrame *auxFrame = resource.value<QTextFrame *>();
    if (auxFrame) {
        if (d->caret.position() == auxFrame->firstPosition() - 1) {
            return true;
        }
        return false;
    }
    return d->caret.atEnd();
}

bool KoTextEditor::atStart() const
{
    return d->caret.atStart();
}

QTextBlock KoTextEditor::block() const
{
    return d->caret.block();
}

QTextCharFormat KoTextEditor::blockCharFormat() const
{
    return d->caret.blockCharFormat();
}

QTextBlockFormat KoTextEditor::blockFormat() const
{
    return d->caret.blockFormat();
}

int KoTextEditor::blockNumber() const
{
    return d->caret.blockNumber();
}

QTextCharFormat KoTextEditor::charFormat() const
{
    return d->caret.charFormat();
}

void KoTextEditor::clearSelection()
{
    d->caret.clearSelection();
}

int KoTextEditor::columnNumber() const
{
    return d->caret.columnNumber();
}

void KoTextEditor::deleteChar()
{
    if (isEditProtected()) {
        return;
    }

    if (!d->caret.hasSelection()) {
        if (d->caret.atEnd())
            return;

        // We also need to refuse delete if it will delete a note frame
        QTextCursor after(d->caret);
        after.movePosition(QTextCursor::NextCharacter);

        QTextFrame *beforeFrame = d->caret.currentFrame();
        while (qobject_cast<QTextTable *>(beforeFrame)) {
            beforeFrame = beforeFrame->parentFrame();
        }

        QTextFrame *afterFrame = after.currentFrame();
        while (qobject_cast<QTextTable *>(afterFrame)) {
            afterFrame = afterFrame->parentFrame();
        }
        if (beforeFrame != afterFrame) {
            return;
        }
    }

    if (!d->deleteInlineObjects(false) || d->caret.hasSelection()) {
        d->updateState(KoTextEditor::Private::Delete, i18n("Delete"));

        QTextCharFormat charFormat = d->caret.charFormat();

        if (!d->caret.hasSelection())
            d->caret.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);

        d->caret.deleteChar();

        d->caret.setCharFormat(charFormat);
    }

    emit cursorPositionChanged();
}

void KoTextEditor::deletePreviousChar()
{
    if (isEditProtected()) {
        return;
    }

    if (!d->caret.hasSelection()) {
        if (d->caret.atStart())
            return;

        // We also need to refuse delete if it will delete a note frame
        QTextCursor after(d->caret);
        after.movePosition(QTextCursor::PreviousCharacter);

        QTextFrame *beforeFrame = d->caret.currentFrame();
        while (qobject_cast<QTextTable *>(beforeFrame)) {
            beforeFrame = beforeFrame->parentFrame();
        }

        QTextFrame *afterFrame = after.currentFrame();
        while (qobject_cast<QTextTable *>(afterFrame)) {
            afterFrame = afterFrame->parentFrame();
        }

        if (beforeFrame != afterFrame) {
            return;
        }
    }

    if (!d->deleteInlineObjects(false) || d->caret.hasSelection()) {
        d->updateState(KoTextEditor::Private::Delete, i18n("Delete"));

        QTextCharFormat charFormat = d->caret.charFormat();

        if (!d->caret.hasSelection())
            d->caret.movePosition(QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor);

        d->caret.deleteChar();

        d->caret.setCharFormat(charFormat);
    }
    emit cursorPositionChanged();
}

const QTextDocument *KoTextEditor::document() const
{
    return d->caret.document();
}

bool KoTextEditor::hasComplexSelection() const
{
    return d->caret.hasComplexSelection();
}

bool KoTextEditor::hasSelection() const
{
    return d->caret.hasSelection();
}


class ProtectionCheckVisitor : public KoTextVisitor
{
public:
    ProtectionCheckVisitor(const KoTextEditor *editor)
        : KoTextVisitor(const_cast<KoTextEditor *>(editor))
    {
    }

    // override super's implementation to not waste cpu cycles
    virtual void visitBlock(QTextBlock , const QTextCursor &)
    {
    }

    virtual void nonVisit()
    {
        setAbortVisiting(true);
    }
};

bool KoTextEditor::isEditProtected(bool useCached) const
{
    ProtectionCheckVisitor visitor(this);

    if (useCached) {
        if (! d->editProtectionCached) {
            recursivelyVisitSelection(d->document->rootFrame()->begin(), visitor);
            d->editProtected = visitor.abortVisiting();
            d->editProtectionCached = true;
        }
        return d->editProtected;
    }
    d->editProtectionCached = false;
    recursivelyVisitSelection(d->document->rootFrame()->begin(), visitor);
    return visitor.abortVisiting();
}

void KoTextEditor::insertBlock()
{
    if (isEditProtected()) {
        return;
    }

    //TODO
}

void KoTextEditor::insertBlock(const QTextBlockFormat &format)
{
    if (isEditProtected()) {
        return;
    }

    Q_UNUSED(format)
    //TODO
}

void KoTextEditor::insertBlock(const QTextBlockFormat &format, const QTextCharFormat &charFormat)
{
    if (isEditProtected()) {
        return;
    }

    Q_UNUSED(format)
    Q_UNUSED(charFormat)
    //TODO
}

void KoTextEditor::insertTable(int rows, int columns)
{
    if (isEditProtected() || rows <= 0 || columns <= 0) {
        return;
    }

    d->updateState(KoTextEditor::Private::Custom, i18n("Insert Table"));
    QTextTableFormat tableFormat;

    tableFormat.setWidth(QTextLength(QTextLength::PercentageLength, 100));
    tableFormat.setMargin(5);

    KoChangeTracker *changeTracker = KoTextDocument(d->document).changeTracker();
    if (changeTracker && changeTracker->recordChanges()) {
        QTextCharFormat charFormat = d->caret.charFormat();
        QTextBlockFormat blockFormat = d->caret.blockFormat();
        QString title = i18n("Insert Table");

        int changeId;
        if (!d->caret.atBlockStart()) {
            changeId = changeTracker->mergeableId(KoGenChange::InsertChange, title, charFormat.intProperty(KoCharacterStyle::ChangeTrackerId));
        } else {
            changeId = changeTracker->mergeableId(KoGenChange::InsertChange, title, blockFormat.intProperty(KoCharacterStyle::ChangeTrackerId));
        }

        if (!changeId) {
            changeId = KoTextDocument(d->document).changeTracker()->getInsertChangeId(title, 0);
        }

        tableFormat.setProperty(KoCharacterStyle::ChangeTrackerId, changeId);
    }

    QTextBlock currentBlock = d->caret.block();
    if (d->caret.position() != currentBlock.position()) {
        d->caret.insertBlock();
        currentBlock = d->caret.block();
    }

    QTextTable *table = d->caret.insertTable(rows, columns, tableFormat);

    // 'Hide' the block before the table
    QTextBlockFormat blockFormat = currentBlock.blockFormat();
    QTextCursor cursor(currentBlock);
    blockFormat.setProperty(KoParagraphStyle::HiddenByTable, true);
    cursor.setBlockFormat(blockFormat);

    // Format the cells a bit.
    for (int row = 0; row < table->rows(); ++row) {
        for (int col = 0; col < table->columns(); ++col) {
            QTextTableCell cell = table->cellAt(row, col);
            QTextTableCellFormat format;
            KoTableCellStyle cellStyle;
            cellStyle.setEdge(KoBorder::Top, KoBorder::BorderSolid, 2, QColor(Qt::black));
            cellStyle.setEdge(KoBorder::Left, KoBorder::BorderSolid, 2, QColor(Qt::black));
            cellStyle.setEdge(KoBorder::Bottom, KoBorder::BorderSolid, 2, QColor(Qt::black));
            cellStyle.setEdge(KoBorder::Right, KoBorder::BorderSolid, 2, QColor(Qt::black));
            cellStyle.setPadding(5);

            cellStyle.applyStyle(format);
            cell.setFormat(format);
        }
    }

    d->updateState(KoTextEditor::Private::NoOp);
    emit cursorPositionChanged();
}

void KoTextEditor::insertTableRowAbove()
{
    if (isEditProtected()) {
        return;
    }

    QTextTable *table = d->caret.currentTable();
    if (table) {
        int changeId = 0;
        KoChangeTracker *changeTracker = KoTextDocument(d->document).changeTracker();
        if (changeTracker && changeTracker->recordChanges()) {
            QString title(i18n("Insert Row Above"));
            changeId = changeTracker->getInsertChangeId(title, 0);
        }
        addCommand(new InsertTableRowCommand(this, table, false, changeId));
    }
}

void KoTextEditor::insertTableRowBelow()
{
    if (isEditProtected()) {
        return;
    }

    QTextTable *table = d->caret.currentTable();
    if (table) {
        int changeId = 0;
        KoChangeTracker *changeTracker = KoTextDocument(d->document).changeTracker();
        if (changeTracker && changeTracker->recordChanges()) {
            QString title(i18n("Insert Row Above"));
            changeId = changeTracker->getInsertChangeId(title, 0);
        }
        addCommand(new InsertTableRowCommand(this, table, true, changeId));
    }
}

void KoTextEditor::insertTableColumnLeft()
{
    if (isEditProtected()) {
        return;
    }

    QTextTable *table = d->caret.currentTable();
    if (table) {
        int changeId = 0;
        KoChangeTracker *changeTracker = KoTextDocument(d->document).changeTracker();
        if (changeTracker && changeTracker->recordChanges()) {
            QString title(i18n("Insert Column Left"));
            changeId = changeTracker->getInsertChangeId(title, 0);
        }
        addCommand(new InsertTableColumnCommand(this, table, false, changeId));
    }
}

void KoTextEditor::insertTableColumnRight()
{
    if (isEditProtected()) {
        return;
    }

    QTextTable *table = d->caret.currentTable();
    if (table) {
        int changeId = 0;
        KoChangeTracker *changeTracker = KoTextDocument(d->document).changeTracker();
        if (changeTracker && changeTracker->recordChanges()) {
            QString title(i18n("Insert Column Right"));
            changeId = changeTracker->getInsertChangeId(title, 0);
        }
        addCommand(new InsertTableColumnCommand(this, table, true, changeId));
    }
}

void KoTextEditor::deleteTableColumn()
{
    if (isEditProtected()) {
        return;
    }

    QTextTable *table = d->caret.currentTable();
    if (table) {
        int changeId = 0;
        KoChangeTracker *changeTracker = KoTextDocument(d->document).changeTracker();
        if (changeTracker && changeTracker->recordChanges()) {
            QString title(i18n("Delete Column"));
            changeId = changeTracker->getDeleteChangeId(title, QTextDocumentFragment(), 0);
        }

        addCommand(new DeleteTableColumnCommand(this, table, changeId));
    }
}

void KoTextEditor::deleteTableRow()
{
    if (isEditProtected()) {
        return;
    }

    QTextTable *table = d->caret.currentTable();
    if (table) {
        KoChangeTracker *changeTracker = KoTextDocument(d->document).changeTracker();
        int changeId = 0;
        if (changeTracker && changeTracker->recordChanges()) {
            QString title(i18n("Delete Row"));
            changeId = changeTracker->getDeleteChangeId(title, QTextDocumentFragment(), 0);
        }
        addCommand(new DeleteTableRowCommand(this, table, changeId));
    }
}

void KoTextEditor::mergeTableCells()
{
    if (isEditProtected()) {
        return;
    }

    d->updateState(KoTextEditor::Private::Custom, i18n("Merge Cells"));

    QTextTable *table = d->caret.currentTable();

    if (table) {
        table->mergeCells(d->caret);
    }

    d->updateState(KoTextEditor::Private::NoOp);
}

void KoTextEditor::splitTableCells()
{
    if (isEditProtected()) {
        return;
    }

    d->updateState(KoTextEditor::Private::Custom, i18n("Split Cells"));

    QTextTable *table = d->caret.currentTable();

    if (table) {
        QTextTableCell cell = table->cellAt(d->caret);
        table->splitCell(cell.row(), cell.column(),  1, 1);
    }

    d->updateState(KoTextEditor::Private::NoOp);
}

KoInlineNote *KoTextEditor::insertFootNote()
{
    d->updateState(KoTextEditor::Private::Custom, i18n("Insert Footnote"));
    KoInlineNote *note = new KoInlineNote(KoInlineNote::Footnote);
    KoInlineTextObjectManager *manager = KoTextDocument(d->document).inlineTextObjectManager();
    manager->insertInlineObject(d->caret,note);
    note->setMotherFrame(KoTextDocument(d->caret.document()).auxillaryFrame());
    cursor()->setPosition(note->textFrame()->lastPosition());
    d->updateState(KoTextEditor::Private::NoOp);
    return note;
}

KoInlineNote *KoTextEditor::insertEndNote()
{
    d->updateState(KoTextEditor::Private::Custom, i18n("Insert Endnote"));
    KoInlineNote *note = new KoInlineNote(KoInlineNote::Endnote);
    KoInlineTextObjectManager *manager = KoTextDocument(d->document).inlineTextObjectManager();
    manager->insertInlineObject(d->caret,note);
    note->setMotherFrame(KoTextDocument(d->caret.document()).auxillaryFrame());
    cursor()->setPosition(note->textFrame()->lastPosition());
    d->updateState(KoTextEditor::Private::NoOp);
    return note;
}

void KoTextEditor::insertTableOfContents(KoTableOfContentsGeneratorInfo *info)
{
    if (isEditProtected()) {
        return;
    }

    d->updateState(KoTextEditor::Private::Custom, i18n("Insert Table Of Contents"));

    QTextBlockFormat tocFormat;
    KoTableOfContentsGeneratorInfo *newToCInfo = info->clone();
    QTextDocument *tocDocument = new QTextDocument();
    tocFormat.setProperty(KoParagraphStyle::TableOfContentsData, QVariant::fromValue<KoTableOfContentsGeneratorInfo *>(newToCInfo) );
    tocFormat.setProperty(KoParagraphStyle::GeneratedDocument, QVariant::fromValue<QTextDocument*>(tocDocument));

    KoChangeTracker *changeTracker = KoTextDocument(d->document).changeTracker();
    if (changeTracker && changeTracker->recordChanges()) {
        QTextCharFormat charFormat = d->caret.charFormat();
        QTextBlockFormat blockFormat = d->caret.blockFormat();
        QString title = i18n("Insert Table Of Contents");

        int changeId;
        if (!d->caret.atBlockStart()) {
            changeId = changeTracker->mergeableId(KoGenChange::InsertChange, title, charFormat.intProperty(KoCharacterStyle::ChangeTrackerId));
        } else {
            changeId = changeTracker->mergeableId(KoGenChange::InsertChange, title, blockFormat.intProperty(KoCharacterStyle::ChangeTrackerId));
        }

        if (!changeId) {
            changeId = KoTextDocument(d->document).changeTracker()->getInsertChangeId(title, 0);
        }

        tocFormat.setProperty(KoCharacterStyle::ChangeTrackerId, changeId);
    }

    d->caret.insertBlock(tocFormat);
    d->caret.movePosition(QTextCursor::Right);

    d->updateState(KoTextEditor::Private::NoOp);
    emit cursorPositionChanged();
}

void KoTextEditor::setTableOfContentsConfig(KoTableOfContentsGeneratorInfo *info, QTextBlock block)
{
    if (isEditProtected()) {
        return;
    }

    KoTableOfContentsGeneratorInfo *newToCInfo=info->clone();

    d->updateState(KoTextEditor::Private::Custom, i18n("Modify Table Of Contents"));

    QTextCursor cursor(block);
    QTextBlockFormat tocBlockFormat=block.blockFormat();

    tocBlockFormat.setProperty(KoParagraphStyle::TableOfContentsData, QVariant::fromValue<KoTableOfContentsGeneratorInfo*>(newToCInfo) );
    cursor.setBlockFormat(tocBlockFormat);

    d->updateState(KoTextEditor::Private::NoOp);
    emit cursorPositionChanged();
    const_cast<QTextDocument *>(document())->markContentsDirty(document()->firstBlock().position(), 0);
}

void KoTextEditor::insertBibliography()
{
    d->updateState(KoTextEditor::Private::Custom, i18n("Insert Bibliography"));

    QTextBlockFormat bibFormat;
    KoBibliographyInfo *info = new KoBibliographyInfo();
    QTextDocument *bibDocument = new QTextDocument();

    bibFormat.setProperty( KoParagraphStyle::BibliographyData, QVariant::fromValue<KoBibliographyInfo*>(info));
    bibFormat.setProperty( KoParagraphStyle::GeneratedDocument, QVariant::fromValue<QTextDocument*>(bibDocument));

    KoChangeTracker *changeTracker = KoTextDocument(d->document).changeTracker();
    if (changeTracker && changeTracker->recordChanges()) {
        QTextCharFormat charFormat = d->caret.charFormat();
        QTextBlockFormat blockFormat = d->caret.blockFormat();
        QString title = i18n("Insert Bibliography");

        int changeId;
        if (!d->caret.atBlockStart()) {
            changeId = changeTracker->mergeableId(KoGenChange::InsertChange, title, charFormat.intProperty(KoCharacterStyle::ChangeTrackerId));
        } else {
            changeId = changeTracker->mergeableId(KoGenChange::InsertChange, title, blockFormat.intProperty(KoCharacterStyle::ChangeTrackerId));
        }

        if (!changeId) {
            changeId = KoTextDocument(d->document).changeTracker()->getInsertChangeId(title, 0);
        }

        bibFormat.setProperty(KoCharacterStyle::ChangeTrackerId, changeId);
    }

    d->caret.insertBlock();
    d->caret.movePosition(QTextCursor::Left);
    d->caret.insertBlock(bibFormat);
    d->caret.movePosition(QTextCursor::Right);

    d->updateState(KoTextEditor::Private::NoOp);
    emit cursorPositionChanged();
}

KoInlineCite *KoTextEditor::insertCitation()
{
    d->updateState(KoTextEditor::Private::Custom, i18n("Add Citation"));

    KoInlineCite *cite = new KoInlineCite(KoInlineCite::Citation);
    KoInlineTextObjectManager *manager = KoTextDocument(d->document).inlineTextObjectManager();
    manager->insertInlineObject(d->caret,cite);

    d->updateState(KoTextEditor::Private::NoOp);
    return cite;
}

void KoTextEditor::insertText(const QString &text)
{
    if (isEditProtected()) {
        return;
    }

    d->updateState(KoTextEditor::Private::KeyPress, i18n("Key Press"));

    //first we make sure that we clear the inlineObject charProperty, if we have no selection
    if (!d->caret.hasSelection() && d->caret.charFormat().hasProperty(KoCharacterStyle::InlineInstanceId))
        d->clearCharFormatProperty(KoCharacterStyle::InlineInstanceId);

    int startPosition = d->caret.position();

    if (d->caret.blockFormat().hasProperty(KoParagraphStyle::HiddenByTable)) {
        d->caret.movePosition(QTextCursor::PreviousCharacter);
        if (startPosition == d->caret.position()) {
            d->newLine();
            d->caret.movePosition(QTextCursor::PreviousCharacter);
        } else {
            d->newLine();
        }
        QTextBlockFormat bf = d->caret.blockFormat();
        bf.clearProperty(KoParagraphStyle::HiddenByTable);
        d->caret.setBlockFormat(bf);

        startPosition = d->caret.position();
    }

    QTextCharFormat format = d->caret.charFormat();
    if (format.hasProperty(KoCharacterStyle::ChangeTrackerId)) {
        format.clearProperty(KoCharacterStyle::ChangeTrackerId);
    }
    d->caret.insertText(text, format);
    int endPosition = d->caret.position();

    //Mark the inserted text
    d->caret.setPosition(startPosition);
    d->caret.setPosition(endPosition, QTextCursor::KeepAnchor);

    registerTrackedChange(d->caret, KoGenChange::InsertChange, i18n("Key Press"), format, format, false);

    d->caret.clearSelection();

    emit cursorPositionChanged();
}

void KoTextEditor::insertText(const QString &text, const QTextCharFormat &format)
{
    if (isEditProtected()) {
        return;
    }

    Q_UNUSED(text)
    Q_UNUSED(format)
    //TODO
}

void KoTextEditor::insertHtml(const QString &html)
{
    if (isEditProtected()) {
        return;
    }

    // XXX: do the changetracking and everything!
    QTextBlock currentBlock = d->caret.block();
    d->caret.insertHtml(html);

    QList<QTextList *> pastedLists;
    KoList *currentPastedList = 0;
    while (currentBlock != d->caret.block()) {
        currentBlock = currentBlock.next();
        QTextList *currentTextList = currentBlock.textList();
        if(currentTextList && !pastedLists.contains(currentBlock.textList())) {
            KoListStyle *listStyle = KoTextDocument(d->document).styleManager()->defaultListStyle()->clone();
            listStyle->setName("");
            listStyle->setStyleId(0);
            currentPastedList = new KoList(d->document, listStyle);
            QTextListFormat currentTextListFormat = currentTextList->format();

            KoListLevelProperties levelProperty = listStyle->levelProperties(currentTextListFormat.indent());
            levelProperty.setStyle(static_cast<KoListStyle::Style>(currentTextListFormat.style()));
            levelProperty.setLevel(currentTextListFormat.indent());
            levelProperty.setListItemPrefix("");
            levelProperty.setListItemSuffix("");
            levelProperty.setListId((KoListStyle::ListIdType)currentTextList);
            listStyle->setLevelProperties(levelProperty);

            currentTextListFormat.setProperty(KoListStyle::Level, currentTextListFormat.indent());
            currentBlock.textList()->setFormat(currentTextListFormat);

            currentPastedList->updateStoredList(currentBlock);
            currentPastedList->setStyle(listStyle);

            pastedLists.append(currentBlock.textList());
        }
    }
}

void KoTextEditor::mergeBlockCharFormat(const QTextCharFormat &modifier)
{
    if (isEditProtected()) {
        return;
    }

    Q_UNUSED(modifier)
    //TODO
}

void KoTextEditor::mergeBlockFormat(const QTextBlockFormat &modifier)
{
    if (isEditProtected()) {
        return;
    }
    d->caret.mergeBlockFormat(modifier);
    emit textFormatChanged();
}

void KoTextEditor::mergeCharFormat(const QTextCharFormat &modifier)
{
    if (isEditProtected()) {
        return;
    }

    Q_UNUSED(modifier)
    //TODO
}

bool KoTextEditor::movePosition(QTextCursor::MoveOperation operation, QTextCursor::MoveMode mode, int n)
{
    d->editProtectionCached = false;

    // We need protection against moving in and out of note areas
    QTextCursor after(d->caret);
    bool b = after.movePosition (operation, mode, n);

    QTextFrame *beforeFrame = d->caret.currentFrame();
    while (qobject_cast<QTextTable *>(beforeFrame)) {
        beforeFrame = beforeFrame->parentFrame();
    }

    QTextFrame *afterFrame = after.currentFrame();
    while (qobject_cast<QTextTable *>(afterFrame)) {
        afterFrame = afterFrame->parentFrame();
    }

    if (beforeFrame == afterFrame) {
        if (after.selectionEnd() == after.document()->characterCount() -1) {
            QVariant resource = after.document()->resource(KoTextDocument::AuxillaryFrame,
            KoTextDocument::AuxillaryFrameURL);
            QTextFrame *auxFrame = resource.value<QTextFrame *>();
            if (auxFrame) {
                if (operation == QTextCursor::End) {
                    d->caret.setPosition(auxFrame->firstPosition() - 1, mode);
                    emit cursorPositionChanged();
                    return true;
                }
                return false;
            }
        }
        d->caret = after;
        emit cursorPositionChanged();
        return b;
    }
    return false;
}

void KoTextEditor::newLine()
{
    if (isEditProtected()) {
        return;
    }

    d->updateState(KoTextEditor::Private::Custom, i18n("New Paragraph"));

    // Handle if this is the special block before a table
    if (d->caret.blockFormat().hasProperty(KoParagraphStyle::HiddenByTable)) {
        //FIXME
        d->caret.movePosition(QTextCursor::PreviousCharacter);
    }

    d->newLine();

    d->updateState(KoTextEditor::Private::NoOp);
    emit cursorPositionChanged();
}

int KoTextEditor::position() const
{
    return d->caret.position();
}

void KoTextEditor::removeSelectedText()
{
    if (isEditProtected()) {
        return;
    }

    // TODO: make the deleting of the inline objects undoable.
    // TODO: make work with complex selections

    // Remove the inline objects in the current selection
    KoInlineTextObjectManager *inlineObjectManager = KoTextDocument(d->document).inlineTextObjectManager();
    KoBookmarkManager *bookmarkManager = inlineObjectManager->bookmarkManager();
    QTextCursor cursor = d->document->find(QString(QChar::ObjectReplacementCharacter), selectionStart());

    // however, if bookmarks span beyond the selection, we shouldn't remove the bookmark inline objects
    // but place them back after we've removed the selection.
    QList<KoBookmark *> bookmarksToBeMoved;
    QList<KoInlineObject*> objectsToBeRemoved;

    while (!cursor.isNull() && cursor.position() <= selectionEnd()) {

        QTextCharFormat fmt = cursor.charFormat();
        KoInlineObject *obj = inlineObjectManager->inlineTextObject(fmt);
        KoBookmark *bookmark = dynamic_cast<KoBookmark*>(obj);
        if (bookmark) {

            KoBookmark::BookmarkType type = bookmark->type();
            if (type == KoBookmark::StartBookmark) {

                KoBookmark *endmark = bookmark->endBookmark();
                Q_ASSERT(endmark);
                if (endmark && endmark->position() > selectionEnd()) {
                    bookmarksToBeMoved << bookmark;
                }
            }
            else if (type == KoBookmark::EndBookmark) {
                KoBookmark *startmark = bookmarkManager->retrieveBookmark(bookmark->name());
                Q_ASSERT(startmark);
                if (startmark && startmark->position() < selectionStart()) {
                    bookmarksToBeMoved << bookmark;
                }

            }
        }
        if (!bookmarksToBeMoved.contains(bookmark)) {
            objectsToBeRemoved << obj;
        }
        cursor = d->document->find(QString(QChar::ObjectReplacementCharacter), cursor.position());
    }
    foreach(KoInlineObject *obj, objectsToBeRemoved) {
        inlineObjectManager->removeInlineObject(obj); // does _not_ remove the character in the text doc
        // Note: do not delete the object here. Deleted objects are stored by the bookmark manager
        //       for future use. Also, start bookmarks might still have a reference to the end bookmark
        //       that is being deleted.
    }

    d->caret.removeSelectedText();

    int currentPosition = d->caret.position();

    // now restore the bookmarks that spanned beyond the selection we removed
    foreach(KoBookmark *bookmark, bookmarksToBeMoved) {

        QTextCharFormat oldCf = d->caret.charFormat();
        // create a new format out of the old so that the current formatting is
        // also used for the inserted object.  KoVariables render text too ;)
        QTextCharFormat cf(oldCf);
        cf.setObjectType(QTextFormat::UserObject + 1);
        cf.setProperty(KoInlineTextObjectManager::InlineInstanceId, bookmark->id());
        cursor.insertText(QString(QChar::ObjectReplacementCharacter), cf);
        // reset to use old format so that the InlineInstanceId is no longer set.
        cursor.setCharFormat(oldCf);
    }

    updateInlineObjectPosition(currentPosition);

    emit cursorPositionChanged();
}

void KoTextEditor::select(QTextCursor::SelectionType selection)
{
    //TODO add selection of previous/next char, and option about hasSelection
    d->caret.select(selection);
}

QString KoTextEditor::selectedText() const
{
    return d->caret.selectedText();
}

QTextDocumentFragment KoTextEditor::selection() const
{
    return d->caret.selection();
}

int KoTextEditor::selectionEnd() const
{
    return d->caret.selectionEnd();
}

int KoTextEditor::selectionStart() const
{
    return d->caret.selectionStart();
}

//void KoTextEditor::setBlockCharFormat(const QTextCharFormat &format)
//{
//    if (isEditProtected()) {
//        return;
//    }

//    Q_UNUSED(format)
//    //TODO
//}

void KoTextEditor::setBlockFormat(const QTextBlockFormat &format)
{
    if (isEditProtected()) {
        return;
    }

    Q_UNUSED(format)
    d->caret.setBlockFormat(format);
    emit textFormatChanged();
}

void KoTextEditor::setCharFormat(const QTextCharFormat &format)
{
    if (isEditProtected()) {
        return;
    }

    d->caret.setCharFormat(format);
    emit textFormatChanged();
}

//void KoTextEditor::setTableFormat(const QTextTableFormat &format)
//{
//    if (isEditProtected()) {
//        return;
//    }

//    Q_UNUSED(format)
//    //TODO
//}

void KoTextEditor::setPosition(int pos, QTextCursor::MoveMode mode)
{
    d->editProtectionCached = false;

    if (pos == d->caret.document()->characterCount() -1) {
        QVariant resource = d->caret.document()->resource(KoTextDocument::AuxillaryFrame,
        KoTextDocument::AuxillaryFrameURL);
        if (resource.isValid()) {
            return;
        }
    }

    if (mode == QTextCursor::MoveAnchor) {
        d->caret.setPosition (pos, mode);
        emit cursorPositionChanged();
    }

    // We need protection against moving in and out of note areas
    QTextCursor after(d->caret);
    after.setPosition (pos, mode);

    QTextFrame *beforeFrame = d->caret.currentFrame();
    while (qobject_cast<QTextTable *>(beforeFrame)) {
        beforeFrame = beforeFrame->parentFrame();
    }

    QTextFrame *afterFrame = after.currentFrame();
    while (qobject_cast<QTextTable *>(afterFrame)) {
        afterFrame = afterFrame->parentFrame();
    }

    if (beforeFrame == afterFrame) {
        d->caret = after;
        emit cursorPositionChanged();
    }
}

void KoTextEditor::setVisualNavigation(bool b)
{
    d->caret.setVisualNavigation (b);
}

bool KoTextEditor::visualNavigation() const
{
    return d->caret.visualNavigation();
}

const QTextFrame *KoTextEditor::currentFrame () const
{
    return d->caret.currentFrame();
}

const QTextList *KoTextEditor::currentList () const
{
    return d->caret.currentList();
}

const QTextTable *KoTextEditor::currentTable () const
{
    return d->caret.currentTable();
}


void KoTextEditor::beginEditBlock()
{
    d->updateState(KoTextEditor::Private::Custom);
    d->caret.beginEditBlock();
}

void KoTextEditor::endEditBlock()
{
    d->caret.endEditBlock();
    d->updateState(KoTextEditor::Private::NoOp);
}

#include <KoTextEditor.moc>
