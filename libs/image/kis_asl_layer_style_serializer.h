/*
 *  SPDX-FileCopyrightText: 2015 Dmitry Kazakov <dimula73@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef __KIS_ASL_LAYER_STYLE_SERIALIZER_H
#define __KIS_ASL_LAYER_STYLE_SERIALIZER_H

#include "kritaimage_export.h"

class QIODevice;
class KoPattern;
class KisResourceModel;

#include "kis_psd_layer_style.h"
#include "asl/kis_asl_callback_object_catcher.h"
#include "KisLocalStrokeResources.h"

class KRITAIMAGE_EXPORT KisAslLayerStyleSerializer
{
public:
    KisAslLayerStyleSerializer();
    ~KisAslLayerStyleSerializer();

    void saveToDevice(QIODevice &device);
    bool saveToFile(const QString& filename);
    void readFromDevice(QIODevice &device);
    bool readFromFile(const QString& filename);

    void assignAllLayerStylesToLayers(KisNodeSP root, const QString &storageLocation);
    static QVector<KisPSDLayerStyleSP> collectAllLayerStyles(KisNodeSP root);

    QVector<KisPSDLayerStyleSP> styles() const;
    void setStyles(const QVector<KisPSDLayerStyleSP> &styles);

    QHash<QString, KoPatternSP> patterns() const;
    QVector<KoAbstractGradientSP> gradients() const;
    QHash<QString, KisPSDLayerStyleSP> stylesHash();


    void registerPSDPattern(const QDomDocument &doc);
    void readFromPSDXML(const QDomDocument &doc);

    QDomDocument formXmlDocument() const;
    QDomDocument formPsdXmlDocument() const;

    bool isInitialized() {
        return m_initialized;
    }

    bool isValid() {
        return isInitialized() && m_isValid;
    }

    static QVector<KoResourceSP> fetchEmbeddedResources(const KisPSDLayerStyle *style);

private:
    void registerPatternObject(const KoPatternSP pattern, const  QString& patternUuid);

    void assignPatternObject(const QString &patternUuid, const QString &patternName, std::function<void(KoPatternSP)> setPattern);
    void assignGradientObject(KoAbstractGradientSP gradient, std::function<void(KoAbstractGradientSP)> setGradient);

    static QVector<KoPatternSP> fetchAllPatterns(const KisPSDLayerStyle *style);

    void newStyleStarted(bool isPsdStructure);
    void connectCatcherToStyle(KisPSDLayerStyle *style, const QString &prefix);

private:
    QHash<QString, KoPatternSP> m_patternsStore;

    KisAslCallbackObjectCatcher m_catcher;
    QVector<KisPSDLayerStyleSP> m_stylesVector;
    QVector<KoAbstractGradientSP> m_gradientsStore;
    QHash<QString, KisPSDLayerStyleSP> m_stylesHash;
    bool m_initialized {false};
    bool m_isValid {true};
    QSharedPointer<KisLocalStrokeResources> m_localResourcesInterface;
};

#endif /* __KIS_ASL_LAYER_STYLE_SERIALIZER_H */
