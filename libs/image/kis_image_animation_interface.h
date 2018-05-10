/*
 *  Copyright (c) 2015 Dmitry Kazakov <dimula73@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef __KIS_IMAGE_ANIMATION_INTERFACE_H
#define __KIS_IMAGE_ANIMATION_INTERFACE_H

#include <QObject>
#include <QScopedPointer>

#include "kis_types.h"
#include "kritaimage_export.h"

class KisUpdatesFacade;
class KisTimeRange;
class KoColor;

namespace KisLayerUtils {
    struct SwitchFrameCommand;
}

class KRITAIMAGE_EXPORT KisImageAnimationInterface : public QObject
{
    Q_OBJECT

public:
    KisImageAnimationInterface(KisImage *image);
    KisImageAnimationInterface(const KisImageAnimationInterface &rhs, KisImage *newImage);
    ~KisImageAnimationInterface() override;

    /**
     * Returns true of the image has at least one animated layer
     */
    bool hasAnimation() const;

    /**
     * Returns currently active frame of the underlying image. Some strokes
     * can override this value and it will report a different value.
     */
    int currentTime() const;

    /**
     * Same as currentTime, except it isn't changed when background strokes
     * are running.
     */
    int currentUITime() const;

    /**
     * While any non-current frame is being regenerated by the
     * strategy, the image is kept in a special state, named
     * 'externalFrameActive'. Is this state the following applies:
     *
     * 1) All the animated paint devices switch its state into
     *    frameId() defined by global time.
     *
     * 2) All animation-not-capable devices switch to a temporary
     *    content device, which *is in undefined state*. The stroke
     *    should regenerate the image projection manually.
     */
    bool externalFrameActive() const;

    void requestTimeSwitchWithUndo(int time);

    void requestTimeSwitchNonGUI(int time, bool useUndo = false);

public Q_SLOTS:
    /**
     * Switches current frame (synchronously) and starts an
     * asynchronous regeneration of the entire image.
     */
    void switchCurrentTimeAsync(int frameId, bool useUndo = false);
public:

    /**
     * Start a background thread that will recalculate some extra frame.
     * The result will be reported using two types of signals:
     *
     * 1) KisImage::sigImageUpdated() will be emitted for every chunk
     *    of updated area.
     *
     * 2) sigFrameReady() will be emitted in the end of the operation.
     *    IMPORTANT: to get the result you must connect to this signal
     *    with Qt::DirectConnection and fetch the result from
     *    frameProjection().  After the signal handler is exited, the
     *    data will no longer be available.
     */
    void requestFrameRegeneration(int frameId, const QRegion &dirtyRegion);


    void notifyNodeChanged(const KisNode *node, const QRect &rect, bool recursive);
    void notifyNodeChanged(const KisNode *node, const QVector<QRect> &rects, bool recursive);
    void invalidateFrames(const KisTimeRange &range, const QRect &rect);

    /**
     * Changes the default color of the "external frame" projection of
     * the image's root layer. Please note that this command should be
     * executed from a context of an exclusive job!
     */
    void setDefaultProjectionColor(const KoColor &color);

    /**
     * The current time range selected by user.
     * @return current time range
     */
    const KisTimeRange& fullClipRange() const;
    void setFullClipRange(const KisTimeRange range);

    void setFullClipRangeStartTime(int column);
    void setFullClipRangeEndTime(int column);


    const KisTimeRange &playbackRange() const;
    void setPlaybackRange(const KisTimeRange range);

    int framerate() const;

    /**
     * @return **absolute** file name of the audio channel file
     */
    QString audioChannelFileName() const;

    /**
     * Sets **absolute** file name of the audio channel file. Don't try to pass
     * a relative path, it'll assert!
     */
    void setAudioChannelFileName(const QString &fileName);

    /**
     * @return is the audio channel is currently muted
     */
    bool isAudioMuted() const;

    /**
     * Mutes the audio channel
     */
    void setAudioMuted(bool value);

    /**
     * Returns the preferred audio value in rangle [0, 1]
     */
    qreal audioVolume() const;

    /**
     * Set the preferred volume for the audio channel in range [0, 1]
     */
    void setAudioVolume(qreal value);

public Q_SLOTS:
    void setFramerate(int fps);
public:

    KisImageWSP image() const;

    int totalLength();
private:
    // interface for:
    friend class KisRegenerateFrameStrokeStrategy;
    friend class KisAnimationFrameCacheTest;
    friend struct KisLayerUtils::SwitchFrameCommand;
    friend class KisImageTest;
    void saveAndResetCurrentTime(int frameId, int *savedValue);
    void restoreCurrentTime(int *savedValue);
    void notifyFrameReady();
    void notifyFrameCancelled();
    KisUpdatesFacade* updatesFacade() const;

    void blockFrameInvalidation(bool value);

    friend class KisSwitchTimeStrokeStrategy;
    void explicitlySetCurrentTime(int frameId);

Q_SIGNALS:
    void sigFrameReady(int time);
    void sigFrameCancelled();
    void sigUiTimeChanged(int newTime);
    void sigFramesChanged(const KisTimeRange &range, const QRect &rect);

    void sigInternalRequestTimeSwitch(int frameId, bool useUndo);

    void sigFramerateChanged();
    void sigFullClipRangeChanged();
    void sigPlaybackRangeChanged();

    /**
     * Emitted when the audio channel of the document is changed
     */
    void sigAudioChannelChanged();

    /**
     * Emitted when audion volume changes. Please note that it doesn't change
     * when you mute the channel! When muting, sigAudioChannelChanged() is used instead!
     */
    void sigAudioVolumeChanged();

private:
    struct Private;
    const QScopedPointer<Private> m_d;
};

#endif /* __KIS_IMAGE_ANIMATION_INTERFACE_H */
