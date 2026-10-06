#include "QueueDesignCapture.h"

#include <QCoreApplication>
#include <QColor>
#include <QDir>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QQuickWindow>
#include <QSaveFile>
#include <QTimer>
#include <QtTest>
#include <QWheelEvent>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <utility>

namespace {
QQuickItem *visualItem(QQuickItem *item,const QString &name)
{
    if (!item) return nullptr;
    if (item->objectName()==name) return item;
    for (QQuickItem *const child:item->childItems())
        if (QQuickItem *const match=visualItem(child,name)) return match;
    return nullptr;
}

class QueueDesignCapture final : public QObject {
public:
    QueueDesignCapture(QQmlApplicationEngine &engine,QString directory)
        : QObject(&engine),m_engine(engine),m_directory(std::move(directory)) {}

    void start()
    {
        if (!QDir().mkpath(m_directory)) {
            qCritical().noquote()<<QStringLiteral("CAPTURE_DIRECTORY: Cannot create %1. Choose a writable export directory.").arg(m_directory);
            QCoreApplication::exit(1);return;
        }
        m_window=m_engine.rootObjects().isEmpty()?nullptr:qobject_cast<QQuickWindow *>(m_engine.rootObjects().first());
        if (!m_window) {check(false,"gallery_window");finish();return;}
        m_capture=visualItem(m_window->contentItem(),QStringLiteral("designCaptureArea"));
        m_preview=visualItem(m_window->contentItem(),QStringLiteral("queueDesignPreview"));
        check(m_capture&&m_preview,"gallery_capture_and_preview");
        if (!m_capture||!m_preview) {finish();return;}
        m_count=m_window->property("variantCount").toInt();
        check(m_count>=12&&m_count<=64,"at_least_twelve_bounded_designs");
        if (m_count<12||m_count>64) {finish();return;}
        setDark(true);
        QTimer::singleShot(100,this,[this] {
            m_contactBackground=m_window->color();
            m_contactBackground.setAlpha(255);
            check(m_window->grabWindow().save(QDir(m_directory).filePath("overview.png")),"overview_saved");
            verifySidebar();
            m_window->setProperty("captureMode",true);
            next();
        });
    }

private:
    void check(bool passed,const QString &name,const QString &detail={})
    {
        m_checks.append(QJsonObject{{"name",name},{"passed",passed},{"detail",detail}});
        if (!passed) {m_failures.append(name);qWarning().noquote()<<QStringLiteral("QUEUE_DESIGN_CHECK: %1 %2").arg(name,detail);}
    }

    bool invoke(QObject *object,const char *method,const QVariant &argument)
    {
        return QMetaObject::invokeMethod(object,method,Q_ARG(QVariant,argument));
    }

    bool invoke(QObject *object,const char *method)
    { return QMetaObject::invokeMethod(object,method); }

    void setDark(bool dark)
    { check(invoke(m_window,"setDarkMode",dark),QStringLiteral("set_theme_%1").arg(dark?"dark":"light")); }

    bool click(QQuickItem *item)
    {
        if (!item||!item->isVisible()||!item->isEnabled()||item->width()<=0||item->height()<=0) return false;
        const QPoint point=item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint();
        if (!QRect(QPoint(),m_window->size()).contains(point)) return false;
        QTest::mouseClick(m_window,Qt::LeftButton,Qt::NoModifier,point);
        QTest::qWait(30);
        return true;
    }

    QRectF sceneRect(const QQuickItem *item) const
    { return item?item->mapRectToScene(QRectF(0,0,item->width(),item->height())):QRectF(); }

    bool whollyVisible(const QQuickItem *item) const
    {
        if (!item||!item->isVisible()||item->width()<=0||item->height()<=0) return false;
        return QRectF(0,0,m_window->width(),m_window->height()).adjusted(-0.5,-0.5,0.5,0.5).contains(sceneRect(item));
    }

    void verifySidebar()
    {
        QQuickItem *const toggle=visualItem(m_window->contentItem(),QStringLiteral("gallerySidebarToggle"));
        const bool before=m_window->property("sidebarCollapsed").toBool();
        const qreal width=m_capture->width();
        check(click(toggle),"sidebar_toggle_available");
        check(m_window->property("sidebarCollapsed").toBool()!=before,"sidebar_collapses");
        check(m_capture->width()>=width,"collapsed_sidebar_preserves_main_width");
        check(click(toggle),"sidebar_reopening_available");
        check(m_window->property("sidebarCollapsed").toBool()==before,"sidebar_restores");
    }

    void next()
    {
        if (m_index>=m_count) {
            if (m_dark) {m_dark=false;m_index=0;setDark(false);}
            else {finish();return;}
        }
        check(invoke(m_window,"selectDesign",m_index),QStringLiteral("select_design_%1").arg(m_index+1));
        QTimer::singleShot(100,this,[this] {
            if (m_dark) verifyPreview();
            check(m_capture->width()>600&&m_capture->height()>=250,variantName()+"_capture_geometry");
            capture();
        });
    }

    QString variantName() const
    { return QStringLiteral("%1-%2").arg(m_index+1,2,10,QLatin1Char('0')).arg(m_dark?"dark":"light"); }

    void verifyPreview()
    {
        const QString prefix=variantName()+"_";
        check(m_preview->property("count").toInt()>=12,prefix+"real_queue_fixture");
        const int count=m_preview->property("count").toInt();
        const int selected=m_preview->property("selectedIndex").toInt();
        const qreal before=m_preview->property("contentX").toDouble();
        QQuickItem *const card=visualItem(m_preview,QStringLiteral("designCard%1").arg(selected+1));
        check(click(card),prefix+"pending_card_clickable");
        check(m_preview->property("selectedIndex").toInt()==selected+1,prefix+"click_changes_cursor");
        check(std::abs(m_preview->property("contentX").toDouble()-before)<0.5,prefix+"click_preserves_scroll");
        check(m_preview->property("count").toInt()==count,prefix+"click_preserves_queue");
        check(invoke(m_preview,"seekTo",1.0),prefix+"seek_end_available");
        QTest::qWait(30);
        check(std::abs(m_preview->property("contentX").toDouble()-m_preview->property("maximumScroll").toDouble())<1.0,prefix+"seek_reaches_last_card");
        check(m_preview->property("activeOffscreen").toBool(),prefix+"current_can_leave_view");
        QQuickItem *const returnButton=visualItem(m_preview,QStringLiteral("designReturn"));
        check(click(returnButton),prefix+"return_control_clickable");
        check(!m_preview->property("activeOffscreen").toBool(),prefix+"return_reveals_current");
        check(m_preview->property("selectedIndex").toInt()==selected+1,prefix+"return_preserves_selection");
        check(invoke(m_preview,"seekTo",-1.0),prefix+"seek_before_start_available");
        QTest::qWait(30);
        check(std::abs(m_preview->property("contentX").toDouble()-m_preview->property("minimumScroll").toDouble())<1.0,prefix+"seek_clamped_at_first_card");
        qreal previous=m_preview->property("contentX").toDouble();
        bool monotonic=true;
        const QPointF wheelPoint=m_preview->mapToScene(QPointF(m_preview->width()/2,m_preview->height()/2));
        for (int step=0;step<36;++step) {
            QWheelEvent wheel(wheelPoint,m_window->mapToGlobal(wheelPoint.toPoint()),QPoint(),QPoint(-120,0),
                              Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
            QTest::lastMouseTimestamp+=25;
            wheel.setTimestamp(static_cast<quint64>(QTest::lastMouseTimestamp));
            QCoreApplication::sendEvent(m_window,&wheel);
            QTest::qWait(10);
            const qreal position=m_preview->property("contentX").toDouble();
            monotonic=monotonic&&position>=previous-0.5;
            previous=position;
        }
        check(monotonic,prefix+"horizontal_wheel_never_reverses");
        check(previous>m_preview->property("minimumScroll").toDouble()+1,prefix+"horizontal_wheel_advances");
        check(m_preview->property("selectedIndex").toInt()==selected+1,prefix+"wheel_preserves_selection");
        check(invoke(m_preview,"resetView"),prefix+"reset_fixture");
        QTest::qWait(30);
        verifyCompactLayout(prefix);
    }

    void verifyCompactLayout(const QString &prefix)
    {
        const QSize normal=m_window->size();
        m_window->resize(900,680);QTest::qWait(50);
        check(m_capture->width()>500,prefix+"compact_window_retains_content_width");
        const QRectF captureRect=sceneRect(m_capture);
        check(captureRect.left()>=-0.5&&captureRect.right()<=m_window->width()+0.5,prefix+"compact_no_horizontal_overflow");
        check(captureRect.top()>=-0.5&&captureRect.bottom()<=m_window->height()+0.5,prefix+"compact_no_vertical_overflow");
        const QStringList controls{"galleryThemeToggle","gallerySidebarToggle","galleryPrevious","galleryNext","galleryReset","designScrollbar"};
        for (const QString &name:controls) {
            QQuickItem *const control=visualItem(m_window->contentItem(),name);
            check(whollyVisible(control),prefix+"compact_control_visible_"+name);
        }
        QQuickItem *const timeline=visualItem(m_preview,QStringLiteral("designTimeline"));
        const int selected=m_preview->property("selectedIndex").toInt();
        QQuickItem *const card=visualItem(m_preview,QStringLiteral("designCard%1").arg(selected));
        QQuickItem *const neighbor=visualItem(m_preview,QStringLiteral("designCard%1").arg(selected+1));
        const QRectF cardRect=sceneRect(card),timelineRect=sceneRect(timeline),neighborRect=sceneRect(neighbor);
        check(card&&timeline&&timelineRect.adjusted(-0.5,-0.5,0.5,0.5).contains(cardRect),prefix+"compact_selected_card_fits_viewport");
        check(card&&neighbor&&cardRect.right()<=neighborRect.left()+0.5,prefix+"compact_cards_do_not_overlap");
        QQuickItem *const scrollbar=visualItem(m_preview,QStringLiteral("designScrollbar"));
        check(scrollbar&&timeline&&!sceneRect(scrollbar).intersects(timelineRect),prefix+"compact_scrollbar_does_not_cover_cards");
        invoke(m_preview,"seekTo",1.0);QTest::qWait(30);
        QQuickItem *const returnButton=visualItem(m_preview,QStringLiteral("designReturn"));
        check(whollyVisible(returnButton),prefix+"compact_return_control_visible");
        check(returnButton&&timeline&&!sceneRect(returnButton).intersects(timelineRect),prefix+"compact_return_does_not_cover_cards");
        check(click(returnButton),prefix+"compact_return_usable");
        check(!m_preview->property("activeOffscreen").toBool(),prefix+"compact_return_reveals_current");
        m_window->resize(normal);QTest::qWait(40);invoke(m_preview,"resetView");
    }

    void capture()
    {
        m_grab=m_capture->grabToImage();
        if (!m_grab) {check(false,variantName()+"_grab_started");++m_index;next();return;}
        auto *const timeout=new QTimer(this);
        timeout->setSingleShot(true);
        QObject::connect(timeout,&QTimer::timeout,this,[this] {check(false,variantName()+"_grab_timeout");finish();});
        timeout->start(5000);
        QObject::connect(m_grab.data(),&QQuickItemGrabResult::ready,this,[this,timeout] {
            timeout->stop();timeout->deleteLater();
            const QSharedPointer<QQuickItemGrabResult> completed=m_grab;
            const QImage image=completed->image();
            check(!image.isNull(),variantName()+"_image_rendered");
            const QString file=variantName()+".png";
            check(image.save(QDir(m_directory).filePath(file)),variantName()+"_image_saved");
            m_files.append(file);
            if (m_dark&&!image.isNull()) m_darkImages.append(image);
            m_grab.clear();++m_index;
            QTimer::singleShot(0,this,[this] {next();});
        });
    }

    void finish()
    {
        if (m_finished) return;
        m_finished=true;
        if (!m_darkImages.isEmpty()) {
            constexpr int columns=2,tileWidth=600,maximumTileHeight=250,gap=12;
            int tileHeight=0;
            for (const QImage &image:m_darkImages)
                tileHeight=std::max(tileHeight,image.size().scaled(QSize(tileWidth,maximumTileHeight),Qt::KeepAspectRatio).height());
            const int rows=(m_darkImages.size()+columns-1)/columns;
            QImage sheet(columns*tileWidth+(columns+1)*gap,rows*tileHeight+(rows+1)*gap,QImage::Format_ARGB32_Premultiplied);
            sheet.fill(m_contactBackground);
            QPainter painter(&sheet);painter.setRenderHint(QPainter::SmoothPixmapTransform);
            for (qsizetype index=0;index<m_darkImages.size();++index) {
                const QImage &image=m_darkImages[index];
                const QSize scaled=image.size().scaled(QSize(tileWidth,tileHeight),Qt::KeepAspectRatio);
                const int x=gap+int(index%columns)*(tileWidth+gap),y=gap+int(index/columns)*(tileHeight+gap);
                painter.drawImage(QRect(QPoint(x,y),scaled),image);
            }
            painter.end();check(sheet.save(QDir(m_directory).filePath("contact-sheet.png")),"contact_sheet_saved");
        }
        const QStringList warnings=m_engine.property("queueDesignWarnings").toStringList();
        check(warnings.isEmpty(),"no_qml_warnings",warnings.join(QLatin1Char('\n')));
        check(m_files.size()==m_count*2,"all_dark_and_light_designs_exported");
        const QJsonObject report{{"success",m_failures.isEmpty()},{"checks",m_checks},{"failures",QJsonArray::fromStringList(m_failures)},
            {"qmlWarnings",QJsonArray::fromStringList(warnings)},{"images",QJsonArray::fromStringList(m_files)},{"variantCount",m_count}};
        QSaveFile output(QDir(m_directory).filePath("report.json"));
        const QByteArray data=QJsonDocument(report).toJson(QJsonDocument::Indented);
        const bool saved=output.open(QIODevice::WriteOnly)&&output.write(data)==data.size()&&output.commit();
        if (!saved) qCritical()<<"CAPTURE_REPORT: Cannot save export verification report.";
        QCoreApplication::exit(saved&&m_failures.isEmpty()?0:1);
    }

    QQmlApplicationEngine &m_engine;
    const QString m_directory;
    QQuickWindow *m_window=nullptr;
    QQuickItem *m_capture=nullptr,*m_preview=nullptr;
    QSharedPointer<QQuickItemGrabResult> m_grab;
    QJsonArray m_checks;
    QStringList m_failures,m_files;
    QList<QImage> m_darkImages;
    QColor m_contactBackground;
    int m_count=0,m_index=0;
    bool m_dark=true,m_finished=false;
};
}

void runQueueDesignCapture(QQmlApplicationEngine &engine,const QString &directory)
{
    auto *const capture=new QueueDesignCapture(engine,directory);
    capture->start();
}
