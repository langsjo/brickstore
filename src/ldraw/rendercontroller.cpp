// Copyright (C) 2004-2025 Robert Griebl
// SPDX-License-Identifier: GPL-3.0-only

#include <cmath>

#include <QtConcurrent>
#include <QRandomGenerator>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QQuick3DTextureData>
#include <QPainter>

#include <QCoro/QCoroFuture>

#include "bricklink/core.h"
#include "library.h"
#include "part.h"
#include "rendercontroller.h"

using namespace std::chrono_literals;

namespace LDraw {

QHash<const BrickLink::Color *, QImage> RenderController::s_materialTextureDatas;


RenderController::RenderController(QObject *parent)
    : QObject(parent)
    , m_lineGeo(new QQuick3DGeometry())
    , m_lines(new QmlRenderLineInstancing())
    , m_clearColor(Qt::white)
{
    static const float lineGeo[] = {
        0, -0.5, 0,
        0, -0.5, 1,
        0, 0.5, 1,
        0, -0.5, 0,
        0, 0.5, 1,
        0, 0.5, 0
    };

    m_lineGeo->setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
    m_lineGeo->setStride(3 * sizeof(float));
    m_lineGeo->addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, QQuick3DGeometry::Attribute::F32Type);
    m_lineGeo->setVertexData(QByteArray::fromRawData(reinterpret_cast<const char *>(lineGeo), sizeof(lineGeo)));
}

RenderController::~RenderController()
{
    qDeleteAll(m_geos);
    delete m_lines;
    delete m_lineGeo;
    if (m_part)
        m_part->release();
}

QQuaternion RenderController::rotateArcBall(QPointF pressPos, QPointF mousePos,
                                            QQuaternion pressRotation, QSizeF viewportSize)
{
    // map the mouse coordinates to the sphere describing this arcball
    auto mapMouseToBall = [=](const QPointF &mouse) -> QVector3D {
        // normalize mouse pos to -1..+1 and reverse y
        QVector3D mouseView(
                    float(2 * mouse.x() / viewportSize.width() - 1),
                    float(1 - 2 * mouse.y() / viewportSize.height()),
                    0
        );

        QVector3D mapped = mouseView; // (mouseView - m_center)* (1.f / m_radius);
        float l2 = mapped.lengthSquared();
        if (l2 > 1.f) {
            mapped.normalize();
            mapped[2] = 0.f;
        } else {
            mapped[2] = std::sqrt(1.f - l2);
        }
        return mapped;
    };

    // re-calculate the rotation given the current mouse position
    auto from = mapMouseToBall(pressPos);
    auto to = mapMouseToBall(mousePos);

    // given these two vectors on the arcball sphere, calculate the quaternion for the arc between
    // this is the correct rotation, but it follows too slow...
    //auto q = QQuaternion::rotationTo(from, to);

    // this one seems to work far better
    auto q = QQuaternion(QVector3D::dotProduct(from, to), QVector3D::crossProduct(from, to));
    return q * pressRotation;
}

const BrickLink::Item *RenderController::item() const
{
    return m_item;
}

const BrickLink::Color *RenderController::color() const
{
    return m_color;
}

QList<QmlRenderGeometry *> RenderController::surfaces()
{
    return m_geos;
}

QQuick3DGeometry *RenderController::lineGeometry()
{
    return m_lineGeo;
}

QQuick3DInstancing *RenderController::lines()
{
    return m_lines;
}

void RenderController::setItemAndColor(BrickLink::QmlItem item, BrickLink::QmlColor color)
{
    const BrickLink::Item *i = item.wrappedObject();
    const BrickLink::Color *c = color.wrappedObject();

    setItemAndColor(i, c);
}

void RenderController::setItemAndColor(const BrickLink::Item *item, const BrickLink::Color *color)
{
    if (!color)
        color = BrickLink::core()->color(0); // not-applicable

    // no change
    if ((item == m_item) && (!item || (color == m_color)))
        return;

    // new item
    auto oldPart = m_part; // do not release immediately, as it might be re-used on color change
    m_part = nullptr;
    m_item = item;
    m_color = color;

    emit canRenderChanged(canRender());

    if (item) {
        LDraw::library()->partFromBrickLinkId(item->id())
            .then(this, [this, item, color](Part *part) {
            if (!part)
                return;
            if (item != m_item) {
                part->release();
                return;
            }

            if (m_part)
                m_part->release();
            m_part = part;
            part->addRef();

            emit canRenderChanged(canRender());

            QtConcurrent::run(&RenderController::calculateRenderData, this, part, color)
                .then(this, [this, color, part](RenderData data) {
                    part->release();
                    if (part != m_part)
                        return;
                    if (color != m_color) {
                        part->release();
                        return;
                    }
                    applyRenderData(data);
                });
        });
    }
    applyRenderData({});

    if (oldPart)
        oldPart->release();
}

bool RenderController::canRender() const
{
    return (m_part);
}

RenderController::RenderData RenderController::calculateRenderData(Part *part, const BrickLink::Color *color)
{
    if (!part)
        return { };
    part->addRef();

    QByteArray lineBuffer;
    QList<QmlRenderGeometry *> geos;
    QVector3D center;
    float radius = 0;
    QHash<const BrickLink::Color *, QByteArray> surfaceBuffers;

    fillVertexBuffers(part, color, color, QMatrix4x4(), false, surfaceBuffers, lineBuffer);

    for (auto it = surfaceBuffers.cbegin(); it != surfaceBuffers.cend(); ++it) {
        const QByteArray &data = it.value();
        if (data.isEmpty())
            continue;

        const BrickLink::Color *surfaceColor = it.key();
        const bool isTextured = surfaceColor->hasParticles() || (surfaceColor->id() == 0);

        const int stride = (3 + 3 + (isTextured ? 2 : 0)) * sizeof(float);

        auto geo = new QmlRenderGeometry(surfaceColor);

        // calculate bounding box
        static constexpr auto fmin = std::numeric_limits<float>::min();
        static constexpr auto fmax = std::numeric_limits<float>::max();

        QVector3D vmin = QVector3D(fmax, fmax, fmax);
        QVector3D vmax = QVector3D(fmin, fmin, fmin);

        for (int i = 0; i < data.size(); i += stride) {
            auto v = reinterpret_cast<const float *>(it->constData() + i);
            vmin = QVector3D(std::min(vmin.x(), v[0]), std::min(vmin.y(), v[1]), std::min(vmin.z(), v[2]));
            vmax = QVector3D(std::max(vmax.x(), v[0]), std::max(vmax.y(), v[1]), std::max(vmax.z(), v[2]));
        }

        // calculate bounding sphere
        QVector3D surfaceCenter = (vmin + vmax) / 2;
        float surfaceRadius = 0;

        for (int i = 0; i < data.size(); i += stride) {
            auto v = reinterpret_cast<const float *>(it->constData() + i);
            surfaceRadius = std::max(surfaceRadius, (surfaceCenter - QVector3D { v[0], v[1], v[2] }).lengthSquared());
        }
        surfaceRadius = std::sqrt(surfaceRadius);

        geo->setPrimitiveType(QQuick3DGeometry::PrimitiveType::Triangles);
        geo->setStride(stride);
        geo->addAttribute(QQuick3DGeometry::Attribute::PositionSemantic, 0, QQuick3DGeometry::Attribute::F32Type);
        geo->addAttribute(QQuick3DGeometry::Attribute::NormalSemantic, 3 * sizeof(float), QQuick3DGeometry::Attribute::F32Type);
        if (isTextured) {
            geo->addAttribute(QQuick3DGeometry::Attribute::TexCoord0Semantic, 6 * sizeof(float), QQuick3DGeometry::Attribute::F32Type);

            QQuick3DTextureData *texData = generateMaterialTextureData(surfaceColor);
            texData->setParentItem(geo);  // 3D scene parent
            texData->setParent(geo);      // owning parent
            geo->setTextureData(texData);
        }
        geo->setBounds(vmin, vmax);
        geo->setCenter(surfaceCenter);
        geo->setRadius(surfaceRadius);
        geo->setVertexData(data);

        geos.append(geo);
    }

    for (auto *geo : std::as_const(geos)) {
        // Merge all the bounding spheres. This is not perfect, but very, very close in most cases
        const auto geoCenter = geo->center();
        const auto geoRadius = geo->radius();

        if (qFuzzyIsNull(radius)) { // first one
            center = geoCenter;
            radius = geoRadius;
        } else {
            QVector3D d = geoCenter - center;
            float l = d.length();

            if ((l + radius) < geoRadius) { // the old one is inside the new one
                center = geoCenter;
                radius = geoRadius;
            } else if ((l + geoRadius) > radius) { // the new one is NOT inside the old one -> we need to merge
                float nr = (radius + l + geoRadius) / 2;
                center = center + (geoCenter - center).normalized() * (nr - radius);
                radius = nr;
            }
        }
    }

    part->release();
    return { lineBuffer, geos, center, radius };
}

void RenderController::applyRenderData(const RenderData &data)
{
    m_lines->clear();
    if (!data.lineBuffer.isEmpty())
        m_lines->setBuffer(data.lineBuffer);
    m_lines->update();
    qDeleteAll(m_geos);
    m_geos = data.geos;

    emit surfacesChanged();

    if (m_center != data.center) {
        m_center = data.center;
        emit centerChanged();
    }
    if (!qFuzzyCompare(m_radius, data.radius)) {
        m_radius = data.radius;
        emit radiusChanged();
    }
    emit itemOrColorChanged();
}

std::vector<std::pair<float, float>> RenderController::uvMapToNearestPlane(const QVector3D &normal,
                                                                           std::initializer_list<const QVector3D> vectors)
{
    const float ax = std::abs(normal.x());
    const float ay = std::abs(normal.y());
    const float az = std::abs(normal.z());

    int uc = 0, vc = 0;

    if ((ax >= ay) && (ax >= az)) {
        uc = 1; vc = 2;
        if (normal.x() < 0)
            std::swap(uc, vc);
    } else if ((ay > ax) && (ay >= az)) {
        uc = 0; vc = 2;
        if (normal.y() < 0)
            std::swap(uc, vc);
    } else if ((az > ax) && (az > ay)) {
        uc = 0; vc = 1;
        if (normal.z() < 0)
            std::swap(uc, vc);
    }

    std::vector<std::pair<float, float>> uv;
    for (auto &&vec : std::as_const(vectors))
        uv.emplace_back(vec[uc] / 24, vec[vc] / 24);

    return uv;
}

void RenderController::fillVertexBuffers(Part *part, const BrickLink::Color *modelColor,
                                         const BrickLink::Color *baseColor,const QMatrix4x4 &matrix,
                                         bool inverted, QHash<const BrickLink::Color *, QByteArray> &surfaceBuffers,
                                         QByteArray &lineBuffer)
{
    if (!part)
        return;

    bool invertNext = false;
    bool ccw = true;

    static auto addFloatsToByteArray = [](QByteArray &buffer, std::initializer_list<float> fs) {
        qsizetype oldSize = buffer.size();
        size_t size = fs.size() * sizeof(float);
        buffer.resize(oldSize + qsizetype(size));
        auto *ptr = reinterpret_cast<float *>(buffer.data() + oldSize);
        memcpy(ptr, fs.begin(), size);
    };

    auto mapColor = [&baseColor, &modelColor](int colorId) -> const BrickLink::Color * {
        auto c = (colorId == 16) ? (baseColor ? baseColor : modelColor)
                                 : BrickLink::core()->colorFromLDrawId(colorId);
        if (!c && colorId >= 256) {
            int newColorId = ((colorId - 256) & 0x0f);
            qCWarning(LogLDraw) << "Dithered colors are not supported, using only one:"
                                << colorId << "->" << newColorId;
            c = BrickLink::core()->colorFromLDrawId(newColorId);
        }
        if (!c) {
            qCWarning(LogLDraw) << "Could not map LDraw color" << colorId;
            c = BrickLink::core()->color(9 /*light gray*/);
        }
        return c;
    };

    auto mapEdgeQColor = [&baseColor, &modelColor](int colorId) -> QColor {
        if (colorId == 24) {
            if (baseColor)
                return baseColor->ldrawEdgeColor();
            else if (modelColor)
                return modelColor->ldrawEdgeColor();
        } else if (auto *c = BrickLink::core()->colorFromLDrawId(colorId)) {
            return c->ldrawColor();
        }
        return Qt::black;
    };

    const auto &elements = part->elements();
    for (const Element *e : elements) {
        bool isBFCCommand = false;
        bool isBFCInvertNext = false;

        switch (e->type()) {
        case Element::Type::BfcCommand: {
            const auto *be = static_cast<const BfcCommandElement *>(e);

            if (be->invertNext()) {
                invertNext = true;
                isBFCInvertNext = true;
            }
            if (be->cw())
                ccw = inverted ? false : true;
            if (be->ccw())
                ccw = inverted ? true : false;

            isBFCCommand = true;
            break;
        }
        case Element::Type::Triangle: {
            const auto te = static_cast<const TriangleElement *>(e);
            const auto color = mapColor(te->color());
            const auto p = te->points();
            const auto p0m = matrix.map(p[0]);
            const auto p1m = matrix.map(ccw ? p[2] : p[1]);
            const auto p2m = matrix.map(ccw ? p[1] : p[2]);
            const auto n = QVector3D::normal(p0m, p1m, p2m);

            if (color->hasParticles() || (color->id() == 0)) {
                const auto uv = uvMapToNearestPlane(n, { p0m, p1m, p2m });

                addFloatsToByteArray(surfaceBuffers[color], {
                    p0m.x(), p0m.y(), p0m.z(), n.x(), n.y(), n.z(), uv[0].first, uv[0].second,
                    p1m.x(), p1m.y(), p1m.z(), n.x(), n.y(), n.z(), uv[1].first, uv[1].second,
                    p2m.x(), p2m.y(), p2m.z(), n.x(), n.y(), n.z(), uv[2].first, uv[2].second });
            } else {
                addFloatsToByteArray(surfaceBuffers[color], {
                    p0m.x(), p0m.y(), p0m.z(), n.x(), n.y(), n.z(),
                    p1m.x(), p1m.y(), p1m.z(), n.x(), n.y(), n.z(),
                    p2m.x(), p2m.y(), p2m.z(), n.x(), n.y(), n.z() });
            }
            break;
        }
        case Element::Type::Quad: {
            const auto qe = static_cast<const QuadElement *>(e);
            const auto color = mapColor(qe->color());
            const auto p = qe->points();
            const auto p0m = matrix.map(p[0]);
            const auto p1m = matrix.map(p[ccw ? 3 : 1]);
            const auto p2m = matrix.map(p[2]);
            const auto p3m = matrix.map(p[ccw ? 1 : 3]);
            const auto n = QVector3D::normal(p0m, p1m, p2m);

            if (color->hasParticles() || (color->id() == 0)) {
                const auto uv = uvMapToNearestPlane(n, { p0m, p1m, p2m, p3m });

                addFloatsToByteArray(surfaceBuffers[color], {
                    p0m.x(), p0m.y(), p0m.z(), n.x(), n.y(), n.z(), uv[0].first, uv[0].second,
                    p1m.x(), p1m.y(), p1m.z(), n.x(), n.y(), n.z(), uv[1].first, uv[1].second,
                    p2m.x(), p2m.y(), p2m.z(), n.x(), n.y(), n.z(), uv[2].first, uv[2].second,
                    p2m.x(), p2m.y(), p2m.z(), n.x(), n.y(), n.z(), uv[2].first, uv[2].second,
                    p3m.x(), p3m.y(), p3m.z(), n.x(), n.y(), n.z(), uv[3].first, uv[3].second,
                    p0m.x(), p0m.y(), p0m.z(), n.x(), n.y(), n.z(), uv[0].first, uv[0].second });
            } else {
                addFloatsToByteArray(surfaceBuffers[color], {
                    p0m.x(), p0m.y(), p0m.z(), n.x(), n.y(), n.z(),
                    p1m.x(), p1m.y(), p1m.z(), n.x(), n.y(), n.z(),
                    p2m.x(), p2m.y(), p2m.z(), n.x(), n.y(), n.z(),
                    p2m.x(), p2m.y(), p2m.z(), n.x(), n.y(), n.z(),
                    p3m.x(), p3m.y(), p3m.z(), n.x(), n.y(), n.z(),
                    p0m.x(), p0m.y(), p0m.z(), n.x(), n.y(), n.z() });
            }
            break;
        }
        case Element::Type::Line: {
            const auto le = static_cast<const LineElement *>(e);
            const auto c = mapEdgeQColor(le->color());
            const auto p = le->points();
            auto p0m = matrix.map(p[0]);
            auto p1m = matrix.map(p[1]);
            QmlRenderLineInstancing::addLineToBuffer(lineBuffer, c, p0m, p1m);
            break;
        }
        case Element::Type::CondLine: {
            const auto cle = static_cast<const CondLineElement *>(e);
            const auto c = mapEdgeQColor(cle->color());
            const auto p = cle->points();
            auto p0m = matrix.map(p[0]);
            auto p1m = matrix.map(p[1]);
            auto p2m = matrix.map(p[2]);
            auto p3m = matrix.map(p[3]);
            QmlRenderLineInstancing::addConditionalLineToBuffer(lineBuffer, c, p0m, p1m, p2m, p3m);
            break;
        }
        case Element::Type::Part: {
            const auto pe = static_cast<const PartElement *>(e);
            bool matrixReversed = (pe->matrix().determinant() < 0);

            fillVertexBuffers(pe->part(), modelColor, mapColor(pe->color()), matrix * pe->matrix(),
                              inverted ^ invertNext ^ matrixReversed, surfaceBuffers, lineBuffer);
            break;
        }
        default:
            break;
        }

        if (!isBFCCommand || !isBFCInvertNext)
            invertNext = false;
    }
}

QQuick3DTextureData *RenderController::generateMaterialTextureData(const BrickLink::Color *color)
{
    static constexpr int GeneratorVersion = 1;

    if (color && (color->hasParticles() || color->id() == 0)) {
        QImage texImage = s_materialTextureDatas.value(color);

        if (texImage.isNull()) {
            QString cacheName;
            const bool isSpeckle = color->isSpeckle();

            if (color->id() == 0) {
                cacheName = u"Not-Applicable"_qs;
            } else {
                cacheName = (isSpeckle ? u"Speckle"_qs : u"Glitter"_qs)
                            + u'_' + color->ldrawColor().name(QColor::HexArgb)
                            + u'_' + color->particleColor().name(QColor::HexArgb)
                            + u'_' + QString::number(double(color->particleMinSize()))
                            + u'_' + QString::number(double(color->particleMaxSize()))
                            + u'_' + QString::number(double(color->particleFraction()));
            }

            static auto cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
            QString cacheFile = cacheDir + u"/ldraw-textures/" + cacheName + u"_v"
                                + QString::number(GeneratorVersion) + u".png";

            if (!texImage.load(cacheFile) || texImage.isNull()) {
                if (color->id() == 0) {
                    constexpr int tiles = 4;
                    constexpr int tileSize = 16;
                    texImage = QImage(tiles * tileSize, tiles * tileSize, QImage::Format_ARGB32);
                    texImage.fill(Qt::white);
                    QPainter p(&texImage);
                    for (int xdiv = 0; xdiv < tiles; ++xdiv) {
                        for (int ydiv = 0; ydiv < tiles; ++ydiv) {
                            if ((xdiv + ydiv) % 2 == 0)
                                p.fillRect(xdiv * tileSize, ydiv * tileSize, tileSize, tileSize, Qt::lightGray);
                        }
                    }
                } else {
                    const int particleSize = 50;

                    QPixmap particle(particleSize, particleSize);
                    if (isSpeckle) {
                        particle.fill(Qt::transparent);
                        QPainter pp(&particle);
                        pp.setRenderHint(QPainter::Antialiasing);
                        pp.setPen(Qt::NoPen);
                        pp.setBrush(color->particleColor());
                        pp.drawEllipse(particle.rect());
                    } else {
                        particle.fill(color->particleColor());
                    }

                    double particleArea = (color->particleMinSize() + color->particleMaxSize()) / 2.;
                    particleArea *= particleArea;
                    if (isSpeckle)
                        particleArea *= (M_PI / 4.);

                    const int texSize = 512; // ~ 24 LDU, the width of a 1 x 1 Brick
                    const double ldus = 24.;
                    int particleCount = int(std::floor((ldus * ldus * color->particleFraction()) / particleArea));
                    int delta = int(std::ceil(color->particleMaxSize() * texSize / ldus));

                    QImage img(texSize + delta * 2, texSize + delta * 2, QImage::Format_ARGB32);
                    // we need to use .rgba() here - otherwise the alpha channel will be pre-multiplied to RGB
                    img.fill(color->ldrawColor().rgba());

                    QList<QPainter::PixmapFragment> fragments;
                    fragments.reserve(particleCount);
                    QRandomGenerator *rd = QRandomGenerator::global();
                    std::uniform_real_distribution<double> dis(color->particleMinSize(),
                                                               color->particleMaxSize());

                    double neededArea = std::floor(texSize * texSize * color->particleFraction());
                    double filledArea = 0;

                    //TODO: maybe partition the square into a grid and use random noise to offset drawing
                    //      into each cell to get a more uniform distribution

                    while (filledArea < neededArea) {
                        double x = rd->bounded(texSize) + delta;
                        double y = rd->bounded(texSize) + delta;
                        double sx = std::max(1. / (particleSize - 5), texSize / (ldus * particleSize) * dis(*rd));
                        double sy = isSpeckle ? sx : std::max(1. / (particleSize - 5), texSize / (ldus * particleSize) * dis(*rd));
                        double rot = isSpeckle ? 0. : rd->bounded(90.);
                        double opacity = isSpeckle ? 1. : std::clamp((rd->bounded(.3) + .7), 0., 1.);

                        double area = particleSize * particleSize * sx * sy;
                        if (isSpeckle)
                            area *= (M_PI / 4.);
                        filledArea += area;

                        fragments << QPainter::PixmapFragment::create({ x, y }, particle.rect(), sx, sy, rot, opacity);

                        // make it seamless
                        if (x < (2 * delta))
                            fragments << QPainter::PixmapFragment::create({ x + texSize, y }, particle.rect(), sx, sy, rot, opacity);
                        else if (x > texSize)
                            fragments << QPainter::PixmapFragment::create({ x - texSize, y }, particle.rect(), sx, sy, rot, opacity);
                        if (y < (2 * delta))
                            fragments << QPainter::PixmapFragment::create({ x, y + texSize }, particle.rect(), sx, sy, rot, opacity);
                        else if (y > texSize)
                            fragments << QPainter::PixmapFragment::create({ x, y - texSize }, particle.rect(), sx, sy, rot, opacity);
                    }

                    QPainter p(&img);
                    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
                    p.drawPixmapFragments(fragments.constData(), int(fragments.count()), particle);
                    p.end();

                    texImage = img.copy(delta, delta, texSize, texSize).rgbSwapped()
                                   .scaled(texSize / 2, texSize / 2, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                }
                QDir(QFileInfo(cacheFile).absolutePath()).mkpath(u"."_qs);
                texImage.save(cacheFile);
            }
            s_materialTextureDatas.insert(color, texImage);
        }

        auto texData = new QQuick3DTextureData();
        texData->setFormat(QQuick3DTextureData::RGBA8);
        texData->setSize(texImage.size());
        texData->setHasTransparency(color->ldrawColor().alpha() < 255);
        texData->setTextureData(QByteArray { reinterpret_cast<const char *>(texImage.constBits()),
                                             texImage.sizeInBytes() });
        return texData;
    }
    return nullptr;
}

void RenderController::resetCamera()
{
    emit qmlResetCamera();
}

QVector3D RenderController::center() const
{
    return m_center;
}

float RenderController::radius() const
{
    return m_radius;
}

bool RenderController::isTumblingAnimationActive() const
{
    return m_tumblingAnimationActive;
}

void RenderController::setTumblingAnimationActive(bool active)
{
    if (m_tumblingAnimationActive != active) {
        m_tumblingAnimationActive = active;
        emit tumblingAnimationActiveChanged();
    }
}

const QColor &RenderController::clearColor() const
{
    return m_clearColor;
}

void RenderController::setClearColor(const QColor &newClearColor)
{
    if (m_clearColor != newClearColor) {
        m_clearColor = newClearColor;
        emit clearColorChanged(m_clearColor);
    }
}

} // namespace LDraw

#include "moc_rendercontroller.cpp"
