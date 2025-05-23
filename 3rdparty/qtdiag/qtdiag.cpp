// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "qtdiag.h"
#include <QtGui/QGuiApplication>
#include <QtGui/QStyleHints>
#include <QtGui/QScreen>
#include <QtGui/QFont>
#include <QtGui/QFontDatabase>
#include <QtGui/QPalette>
#ifndef QT_NO_OPENGL
#  include <QtGui/QOpenGLContext>
#  include <QtGui/QOpenGLFunctions>
#  include <QtOpenGL/QOpenGLVersionProfile>
#  include <QtOpenGL/QOpenGLVersionFunctions>
#  include <QtOpenGL/QOpenGLVersionFunctionsFactory>
#endif // QT_NO_OPENGL
#if QT_CONFIG(vulkan) && defined(HAS_VULKAN)
#  include <QtGui/QVulkanInstance>
#  include <QtGui/QVulkanWindow>
#endif // vulkan
#include <QtGui/QWindow>
#include <QtGui/QInputDevice>

#ifdef NETWORK_DIAG
#  include <QSslSocket>
#endif

#include <QtCore/QLibraryInfo>
#include <QtCore/QStringList>
#include <QtCore/QVariant>
#include <QtCore/QSysInfo>
#include <QtCore/QLibraryInfo>
#if QT_CONFIG(processenvironment)
#  include <QtCore/QProcessEnvironment>
#endif
#include <QtCore/QTextStream>
#include <QtCore/QStandardPaths>
#include <QtCore/QDir>
#include <QtCore/QFileSelector>
#include <QtCore/QDebug>
#include <QtCore/QVersionNumber>

#include <private/qsimd_p.h>
#ifdef Q_CC_MSVC
#  pragma warning(push)
#  pragma warning(disable: 4458)
#  pragma warning(disable: 4201)
#endif
#include <private/qguiapplication_p.h>
#ifdef Q_CC_MSVC
#  pragma warning(pop)
#endif
#include <qpa/qplatformintegration.h>
#include <qpa/qplatformscreen.h>
#include <qpa/qplatformtheme.h>
#include <qpa/qplatformthemefactory_p.h>
#include <qpa/qplatformintegration.h>
#include <private/qhighdpiscaling_p.h>

#include <QtGui/private/qrhi_p.h>
#include <QtGui/QOffscreenSurface>
#if QT_CONFIG(opengl) && defined(Q_OS_LINUX)
# include <QtGui/private/qrhigles2_p.h>
#endif
#if QT_CONFIG(vulkan) && defined(HAS_VULKAN)
# include <QtGui/private/qrhivulkan_p.h>
#endif
#ifdef Q_OS_WIN
#include <QtGui/private/qrhid3d11_p.h>
#endif
#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
# include <QtGui/private/qrhimetal_p.h>
#endif

#ifdef QT_WIDGETS_LIB
#  include <QtWidgets/QStyleFactory>
#endif

#include <algorithm>

QT_BEGIN_NAMESPACE

QTextStream &operator<<(QTextStream &str, const QSize &s)
{
    str << s.width() << 'x' << s.height();
    return str;
}

QTextStream &operator<<(QTextStream &str, const QSizeF &s)
{
    str << s.width() << 'x' << s.height();
    return str;
}

QTextStream &operator<<(QTextStream &str, const QDpi &d)
{
    str << d.first << ',' << d.second;
    return str;
}

QTextStream &operator<<(QTextStream &str, const QRect &r)
{
    str << r.size() << Qt::forcesign << r.x() << r.y() << Qt::noforcesign;
    return str;
}

QTextStream &operator<<(QTextStream &str, const QStringList &l)
{
    for (int i = 0; i < l.size(); ++i) {
        if (i)
            str << ',';
        str << l.at(i);
    }
    return str;
}

QTextStream &operator<<(QTextStream &str, const QFont &f)
{
    str << '"' << f.family() << "\" "  << f.pointSize();
    return str;
}

QTextStream &operator<<(QTextStream &str, QPlatformScreen::SubpixelAntialiasingType st)
{
    static const char *enumValues[] = {
        "Subpixel_None", "Subpixel_RGB", "Subpixel_BGR", "Subpixel_VRGB", "Subpixel_VBGR"
    };
    str << (size_t(st) < sizeof(enumValues) / sizeof(enumValues[0])
            ? enumValues[st] : "<Unknown>");
    return str;
}

QTextStream &operator<<(QTextStream &str, const QRhiDriverInfo &info)
{
    static const char *enumValues[] = {
        "Unknown", "Integrated", "Discrete", "External", "Virtual", "Cpu"
    };
    str << "Device: " << info.deviceName
        << " Device ID: 0x" << Qt::hex << info.deviceId
        << " Vendor ID: 0x" << info.vendorId << Qt::dec
        << " Device type: " << (size_t(info.deviceType) < sizeof(enumValues) / sizeof(enumValues[0])
                                ? enumValues[info.deviceType] : "<Unknown>");
    return str;
}

#if !defined(QT_NO_OPENGL) && defined(Q_OS_LINUX)

QTextStream &operator<<(QTextStream &str, const QSurfaceFormat &format)
{
    str << "Version: " << format.majorVersion() << '.'
        << format.minorVersion() << " Profile: " << format.profile()
        << " Swap behavior: " << format.swapBehavior()
        << " Buffer size (RGB";
    if (format.hasAlpha())
        str << 'A';
    str << "): " << format.redBufferSize() << ',' << format.greenBufferSize()
        << ',' << format.blueBufferSize();
    if (format.hasAlpha())
        str << ',' << format.alphaBufferSize();
    if (const int dbs = format.depthBufferSize())
        str << " Depth buffer: " << dbs;
    if (const int sbs = format.stencilBufferSize())
        str << " Stencil buffer: " << sbs;
    const int samples = format.samples();
    if (samples > 0)
        str << " Samples: " << samples;
    return str;
}

void dumpGlInfo(QTextStream &str, bool listExtensions)
{
    QOpenGLContext context;
    if (context.create()) {
#  ifdef QT_OPENGL_DYNAMIC
        str << "Dynamic GL ";
#  endif
        switch (context.openGLModuleType()) {
        case QOpenGLContext::LibGL:
            str << "LibGL";
            break;
        case QOpenGLContext::LibGLES:
            str << "LibGLES";
            break;
        }
        QWindow window;
        window.setSurfaceType(QSurface::OpenGLSurface);
        window.create();
        context.makeCurrent(&window);
        QOpenGLFunctions functions(&context);

        str << " Vendor: " << reinterpret_cast<const char *>(functions.glGetString(GL_VENDOR))
            << "\nRenderer: " << reinterpret_cast<const char *>(functions.glGetString(GL_RENDERER))
            << "\nVersion: " << reinterpret_cast<const char *>(functions.glGetString(GL_VERSION))
            << "\nShading language: " << reinterpret_cast<const char *>(functions.glGetString(GL_SHADING_LANGUAGE_VERSION))
            <<  "\nFormat: " << context.format();
#  if !QT_CONFIG(opengles2)
        GLint majorVersion;
        functions.glGetIntegerv(GL_MAJOR_VERSION, &majorVersion);
        GLint minorVersion;
        functions.glGetIntegerv(GL_MINOR_VERSION, &minorVersion);
        const QByteArray openGlVersionFunctionsName = "QOpenGLFunctions_"
            + QByteArray::number(majorVersion) + '_' + QByteArray::number(minorVersion);
        str << "\nProfile: None (" << openGlVersionFunctionsName << ')';
        if (majorVersion > 3 || (majorVersion == 3 && minorVersion >= 1)) {
            QOpenGLVersionProfile profile;
            profile.setVersion(majorVersion, minorVersion);
            profile.setProfile(QSurfaceFormat::CoreProfile);
            if (auto f = QOpenGLVersionFunctionsFactory::get(profile, &context)) {
                if (f->initializeOpenGLFunctions())
                    str << ", Core (" << openGlVersionFunctionsName << "_Core)";
            }
            profile.setProfile(QSurfaceFormat::CompatibilityProfile);
            if (auto f = QOpenGLVersionFunctionsFactory::get(profile, &context)) {
                if (f->initializeOpenGLFunctions())
                    str << ", Compatibility (" << openGlVersionFunctionsName << "_Compatibility)";
            }
        }
        str << '\n';
#  endif // !QT_CONFIG(opengles2)
        if (listExtensions) {
            QByteArrayList extensionList = context.extensions().values();
            std::sort(extensionList.begin(), extensionList.end());
            str << " \nFound " << extensionList.size() << " extensions:\n";
            for (const QByteArray &extension : std::as_const(extensionList))
                str << "  " << extension << '\n';
        }
    } else {
        str << "Unable to create an Open GL context.\n";
    }
}

#endif // !QT_NO_OPENGL

#if QT_CONFIG(vulkan) && defined(HAS_VULKAN)
QVersionNumber vulkanVersion(uint32_t v)
{
    return QVersionNumber(VK_VERSION_MAJOR(v), VK_VERSION_MINOR(v), VK_VERSION_PATCH(v));
}

void dumpVkInfo(QTextStream &str)
{
    QVulkanInstance inst;
    if (inst.create()) {
        str << "Vulkan instance available\n";
        str << "Supported instance extensions:\n";
        for (const QVulkanExtension &ext : inst.supportedExtensions())
            str << "  " << ext.name << ", version " << ext.version << "\n";
        str << "Supported layers:\n";
        for (const QVulkanLayer &layer : inst.supportedLayers())
            str << "  " << layer.name << ", version " << layer.version
                << ", spec version " << layer.specVersion.toString()
                << ", " << layer.description << "\n";
        // Show at least the available physical devices. Anything additional
        // needs lots of initialization, or, if done through QVulkanWindow, an
        // exposed window. None of these are very tempting right now.
        str << "Available physical devices:\n";
        QVulkanWindow window;
        window.setVulkanInstance(&inst);
        for (const VkPhysicalDeviceProperties &props : window.availablePhysicalDevices()) {
            str << "  API version " << vulkanVersion(props.apiVersion).toString()
                << Qt::hex << ", vendor 0x" << props.vendorID
                << ", device 0x" << props.deviceID << ", " << props.deviceName
                << Qt::dec << ", type " << props.deviceType
                << ", driver version " << vulkanVersion(props.driverVersion).toString();
        }
    } else {
        str << "Unable to create a Vulkan instance, error code is" << inst.errorCode() << "\n";
    }
}
#endif // vulkan

void dumpRhiBackendInfo(QTextStream &str, const char *name, QRhi::Implementation impl, QRhiInitParams *initParams)
{
    struct RhiFeature {
        const char *name;
        QRhi::Feature val;
    };
    const RhiFeature features[] = {
        { "MultisampleTexture", QRhi::MultisampleTexture },
        { "MultisampleRenderBuffer", QRhi::MultisampleRenderBuffer },
        { "DebugMarkers", QRhi::DebugMarkers },
        { "Timestamps", QRhi::Timestamps },
        { "Instancing", QRhi::Instancing },
        { "CustomInstanceStepRate", QRhi::CustomInstanceStepRate },
        { "PrimitiveRestart", QRhi::PrimitiveRestart },
        { "NonDynamicUniformBuffers", QRhi::NonDynamicUniformBuffers },
        { "NonFourAlignedEffectiveIndexBufferOffset", QRhi::NonFourAlignedEffectiveIndexBufferOffset },
        { "NPOTTextureRepeat", QRhi::NPOTTextureRepeat },
        { "RedOrAlpha8IsRed", QRhi::RedOrAlpha8IsRed },
        { "ElementIndexUint", QRhi::ElementIndexUint },
        { "Compute", QRhi::Compute },
        { "WideLines", QRhi::WideLines },
        { "VertexShaderPointSize", QRhi::VertexShaderPointSize },
        { "BaseVertex", QRhi::BaseVertex },
        { "BaseInstance", QRhi::BaseInstance },
        { "TriangleFanTopology", QRhi::TriangleFanTopology },
        { "ReadBackNonUniformBuffer", QRhi::ReadBackNonUniformBuffer },
        { "ReadBackNonBaseMipLevel", QRhi::ReadBackNonBaseMipLevel },
        { "TexelFetch", QRhi::TexelFetch },
        { "RenderToNonBaseMipLevel", QRhi::RenderToNonBaseMipLevel },
        { "IntAttributes", QRhi::IntAttributes },
        { "ScreenSpaceDerivatives", QRhi::ScreenSpaceDerivatives },
        { "ReadBackAnyTextureFormat", QRhi::ReadBackAnyTextureFormat },
        { "PipelineCacheDataLoadSave", QRhi::PipelineCacheDataLoadSave },
        { "ImageDataStride", QRhi::ImageDataStride },
        { "RenderBufferImport", QRhi::RenderBufferImport },
        { "ThreeDimensionalTextures", QRhi::ThreeDimensionalTextures },
        { "RenderTo3DTextureSlice", QRhi::RenderTo3DTextureSlice },
        { "TextureArrays", QRhi::TextureArrays },
        { nullptr, QRhi::Feature(0) }
    };
    struct RhiTextureFormat {
        const char *name;
        QRhiTexture::Format val;
    };
    const RhiTextureFormat textureFormats[] = {
        { "RGBA8", QRhiTexture::RGBA8 },
        { "BGRA8", QRhiTexture::BGRA8 },
        { "R8", QRhiTexture::R8 },
        { "R16", QRhiTexture::R16 },
        { "RG8", QRhiTexture::RG8 },
        { "RED_OR_ALPHA8", QRhiTexture::RED_OR_ALPHA8 },
        { "RGBA16F", QRhiTexture::RGBA16F },
        { "RGBA32F", QRhiTexture::RGBA32F },
        { "R16F", QRhiTexture::R16F },
        { "R32F", QRhiTexture::R32F },
        { "D16", QRhiTexture::D16 },
        { "D32F", QRhiTexture::D32F },
        { "BC1", QRhiTexture::BC1 },
        { "BC2", QRhiTexture::BC2 },
        { "BC3", QRhiTexture::BC3 },
        { "BC4", QRhiTexture::BC4 },
        { "BC5", QRhiTexture::BC5 },
        { "BC6H", QRhiTexture::BC6H },
        { "BC7", QRhiTexture::BC7 },
        { "ETC2_RGB8", QRhiTexture::ETC2_RGB8 },
        { "ETC2_RGB8A1", QRhiTexture::ETC2_RGB8A1 },
        { "ETC2_RGBA8", QRhiTexture::ETC2_RGBA8 },
        { "ASTC_4x4", QRhiTexture::ASTC_4x4 },
        { "ASTC_5x4", QRhiTexture::ASTC_5x4 },
        { "ASTC_5x5", QRhiTexture::ASTC_5x5 },
        { "ASTC_6x5", QRhiTexture::ASTC_6x5 },
        { "ASTC_6x6", QRhiTexture::ASTC_6x6 },
        { "ASTC_8x5", QRhiTexture::ASTC_8x5 },
        { "ASTC_8x6", QRhiTexture::ASTC_8x6 },
        { "ASTC_8x8", QRhiTexture::ASTC_8x8 },
        { "ASTC_10x5", QRhiTexture::ASTC_10x5 },
        { "ASTC_10x6", QRhiTexture::ASTC_10x6 },
        { "ASTC_10x8", QRhiTexture::ASTC_10x8 },
        { "ASTC_10x10", QRhiTexture::ASTC_10x10 },
        { "ASTC_12x10", QRhiTexture::ASTC_12x10 },
        { "ASTC_12x12", QRhiTexture::ASTC_12x12 },
        { nullptr, QRhiTexture::UnknownFormat }
    };

    QScopedPointer<QRhi> rhi(QRhi::create(impl, initParams, QRhi::Flags(), nullptr));
    if (rhi) {
        str << name << ":\n";
        str << "  Driver Info: " << rhi->driverInfo() << "\n";
        str << "  Min Texture Size: " << rhi->resourceLimit(QRhi::TextureSizeMin) << "\n";
        str << "  Max Texture Size: " << rhi->resourceLimit(QRhi::TextureSizeMax) << "\n";
        str << "  Max Color Attachments: " << rhi->resourceLimit(QRhi::MaxColorAttachments) << "\n";
        str << "  Frames in Flight: " << rhi->resourceLimit(QRhi::FramesInFlight) << "\n";
        str << "  Async Readback Limit: " << rhi->resourceLimit(QRhi::MaxAsyncReadbackFrames) << "\n";
        str << "  MaxThreadGroupsPerDimension: " << rhi->resourceLimit(QRhi::MaxThreadGroupsPerDimension) << "\n";
        str << "  MaxThreadsPerThreadGroup: " << rhi->resourceLimit(QRhi::MaxThreadsPerThreadGroup) << "\n";
        str << "  MaxThreadGroupX: " << rhi->resourceLimit(QRhi::MaxThreadGroupX) << "\n";
        str << "  MaxThreadGroupY: " << rhi->resourceLimit(QRhi::MaxThreadGroupY) << "\n";
        str << "  MaxThreadGroupZ: " << rhi->resourceLimit(QRhi::MaxThreadGroupZ) << "\n";
        str << "  TextureArraySizeMax: " << rhi->resourceLimit(QRhi::TextureArraySizeMax) << "\n";
        str << "  MaxUniformBufferRange: " << rhi->resourceLimit(QRhi::MaxUniformBufferRange) << "\n";
        str << "  Uniform Buffer Alignment: " << rhi->ubufAlignment() << "\n";
        QByteArrayList supportedSampleCounts;
        for (int s : rhi->supportedSampleCounts())
            supportedSampleCounts << QByteArray::number(s);
        str << "  Supported MSAA sample counts: " << supportedSampleCounts.join(',') << "\n";
        str << "  Features:\n";
        for (int i = 0; features[i].name; i++) {
            str << "    " << (rhi->isFeatureSupported(features[i].val) ? "v" : "-") << " " << features[i].name << "\n";
        }
        str << "  Texture formats:";
        for (int i = 0; textureFormats[i].name; i++) {
            if (rhi->isTextureFormatSupported(textureFormats[i].val))
                str << " " << textureFormats[i].name;
        }
        str << "\n";
    }
}

void dumpRhiInfo(QTextStream &str)
{
    str << "Qt Rendering Hardware Interface supported backends:\n";

#if QT_CONFIG(opengl) && defined(Q_OS_LINUX)
    {
        QRhiGles2InitParams params;
        params.fallbackSurface = QRhiGles2InitParams::newFallbackSurface();
        dumpRhiBackendInfo(str, "OpenGL (with default QSurfaceFormat)", QRhi::OpenGLES2, &params);
        delete params.fallbackSurface;
    }
#endif

#if QT_CONFIG(vulkan) && defined(HAS_VULKAN)
    {
        QVulkanInstance vulkanInstance;
        vulkanInstance.create();
        QRhiVulkanInitParams params;
        params.inst = &vulkanInstance;
        dumpRhiBackendInfo(str, "Vulkan", QRhi::Vulkan, &params);
        vulkanInstance.destroy();
    }
#endif

#ifdef Q_OS_WIN
    {
        QRhiD3D11InitParams params;
        dumpRhiBackendInfo(str, "Direct3D 11", QRhi::D3D11, &params);
    }
#endif

#if defined(Q_OS_MACOS) || defined(Q_OS_IOS)
    {
        QRhiMetalInitParams params;
        dumpRhiBackendInfo(str, "Metal", QRhi::Metal, &params);
    }
#endif
}

#define DUMP_CAPABILITY(str, integration, capability) \
    if (platformIntegration->hasCapability(QPlatformIntegration::capability)) \
        str << ' ' << #capability;

// Dump values of QStandardPaths, indicate writable locations by asterisk.
static void dumpStandardLocation(QTextStream &str, QStandardPaths::StandardLocation location)
{
    str << '"' << QStandardPaths::displayName(location) << '"';
    const QStringList directories = QStandardPaths::standardLocations(location);
    const QString writableDirectory = QStandardPaths::writableLocation(location);
    const int writableIndex = writableDirectory.isEmpty() ? -1 : int(directories.indexOf(writableDirectory));
    for (int i = 0; i < directories.size(); ++i) {
        str << ' ';
        if (i == writableIndex)
            str << '*';
        str << QDir::toNativeSeparators(directories.at(i));
        if (i == writableIndex)
            str << '*';
    }
    if (!writableDirectory.isEmpty() && writableIndex < 0)
        str << " *" << QDir::toNativeSeparators(writableDirectory) << '*';
}

#define DUMP_CPU_FEATURE(feature, name)                 \
    if (qCpuHasFeature(feature))                        \
        str << " " name

#define DUMP_STANDARDPATH(str, location) \
    str << "  " << #location << ": "; \
    dumpStandardLocation(str, QStandardPaths::location); \
    str << '\n';

#define DUMP_LIBRARYPATH(str, loc) \
    str << "  " << #loc << ": " << QDir::toNativeSeparators(QLibraryInfo::path(QLibraryInfo::loc)) << '\n';

// Helper to format a type via QDebug to be used for QFlags/Q_ENUM.
template <class T>
static QString formatQDebug(T t)
{
    QString result;
    QDebug(&result) << t;
    return result;
}

// Helper to format a type via QDebug, stripping the class name.
template <class T>
static QString formatValueQDebug(T t)
{
    QString result = formatQDebug(t).trimmed();
    if (result.endsWith(QLatin1Char(')'))) {
        result.chop(1);
        result.remove(0, result.indexOf(QLatin1Char('(')) + 1);
    }
    return result;
}

QTextStream &operator<<(QTextStream &str, const QPalette &palette)
{
    for (int r = 0; r < int(QPalette::NColorRoles); ++r) {
        const QPalette::ColorRole role = static_cast< QPalette::ColorRole>(r);
        const QColor color = palette.color(QPalette::Active, role);
        if (color.isValid())
            str << "  " << formatValueQDebug(role) << ": " << color.name(QColor::HexArgb) << '\n';
    }
    return str;
}

static inline QByteArrayList qtFeatures()
{
    QByteArrayList result;
#ifdef QT_NO_CLIPBOARD
    result.append("QT_NO_CLIPBOARD");
#endif
#ifdef QT_NO_CONTEXTMENU
    result.append("QT_NO_CONTEXTMENU");
#endif
#ifdef QT_NO_CURSOR
    result.append("QT_NO_CURSOR");
#endif
#ifdef QT_NO_DRAGANDDROP
    result.append("QT_NO_DRAGANDDROP");
#endif
#ifdef QT_NO_EXCEPTIONS
    result.append("QT_NO_EXCEPTIONS");
#endif
#ifdef QT_NO_LIBRARY
    result.append("QT_NO_LIBRARY");
#endif
#ifdef QT_NO_NETWORK
    result.append("QT_NO_NETWORK");
#endif
#ifdef QT_NO_OPENGL
    result.append("QT_NO_OPENGL");
#endif
#ifdef QT_NO_OPENSSL
    result.append("QT_NO_OPENSSL");
#endif
#ifdef QT_NO_PROCESS
    result.append("QT_NO_PROCESS");
#endif
#ifdef QT_NO_PRINTER
    result.append("QT_NO_PRINTER");
#endif
#ifdef QT_NO_SESSIONMANAGER
    result.append("QT_NO_SESSIONMANAGER");
#endif
#ifdef QT_NO_SETTINGS
    result.append("QT_NO_SETTINGS");
#endif
#ifdef QT_NO_SHORTCUT
    result.append("QT_NO_SHORTCUT");
#endif
#ifdef QT_NO_SYSTEMTRAYICON
    result.append("QT_NO_SYSTEMTRAYICON");
#endif
#ifdef QT_NO_QTHREAD
    result.append("QT_NO_QTHREAD");
#endif
#ifdef QT_NO_WHATSTHIS
    result.append("QT_NO_WHATSTHIS");
#endif
#ifdef QT_NO_WIDGETS
    result.append("QT_NO_WIDGETS");
#endif
#ifdef QT_NO_ZLIB
    result.append("QT_NO_ZLIB");
#endif
    return result;
}

QString qtDiag(unsigned flags)
{
    QString result;
    QTextStream str(&result);

    const QPlatformIntegration *platformIntegration = QGuiApplicationPrivate::platformIntegration();
    str << QLibraryInfo::build() << " on \"" << QGuiApplication::platformName() << "\" "
              << '\n'
        << "OS: " << QSysInfo::prettyProductName()
              << " [" << QSysInfo::kernelType() << " version " << QSysInfo::kernelVersion() << "]\n";

    str << "\nArchitecture: " << QSysInfo::currentCpuArchitecture() << "; features:";
#if defined(Q_PROCESSOR_X86)
    DUMP_CPU_FEATURE(HYBRID, "hybrid");
    DUMP_CPU_FEATURE(SSE2, "SSE2");
    DUMP_CPU_FEATURE(SSE3, "SSE3");
    DUMP_CPU_FEATURE(SSSE3, "SSSE3");
    DUMP_CPU_FEATURE(SSE4_1, "SSE4.1");
    DUMP_CPU_FEATURE(SSE4_2, "SSE4.2");
    DUMP_CPU_FEATURE(AVX, "AVX");
    DUMP_CPU_FEATURE(AVX2, "AVX2");
    DUMP_CPU_FEATURE(AVX512F, "AVX512F");
    DUMP_CPU_FEATURE(AVX512IFMA, "AVX512IFMA");
    DUMP_CPU_FEATURE(AVX512VBMI2, "AVX512VBMI2");
    DUMP_CPU_FEATURE(AVX512FP16, "AVX512FP16");
    DUMP_CPU_FEATURE(RDRND, "RDRAND");
    DUMP_CPU_FEATURE(RDSEED, "RDSEED");
    DUMP_CPU_FEATURE(AES, "AES");
    DUMP_CPU_FEATURE(VAES, "VAES");
    DUMP_CPU_FEATURE(SHA, "SHA");
#elif defined(Q_PROCESSOR_ARM)
    DUMP_CPU_FEATURE(ARM_NEON, "Neon");
#elif defined(Q_PROCESSOR_MIPS)
    DUMP_CPU_FEATURE(DSP, "DSP");
    DUMP_CPU_FEATURE(DSPR2, "DSPR2");
#endif
    str << '\n';

#if QT_CONFIG(process)
    const QProcessEnvironment systemEnvironment = QProcessEnvironment::systemEnvironment();
    str << "\nEnvironment:\n";
    const QStringList keys = systemEnvironment.keys();
    for (const QString &key : keys) {
        if (key.startsWith(QLatin1Char('Q')))
           str << "  " << key << "=\"" << systemEnvironment.value(key) << "\"\n";
    }
#endif // QT_CONFIG(process)

    const QByteArrayList features = qtFeatures();
    if (!features.isEmpty())
        str << "\nFeatures: " << features.join(' ') << '\n';

    str << "\nLibrary info:\n";
    DUMP_LIBRARYPATH(str, PrefixPath)
    DUMP_LIBRARYPATH(str, DocumentationPath)
    DUMP_LIBRARYPATH(str, HeadersPath)
    DUMP_LIBRARYPATH(str, LibrariesPath)
    DUMP_LIBRARYPATH(str, LibraryExecutablesPath)
    DUMP_LIBRARYPATH(str, BinariesPath)
    DUMP_LIBRARYPATH(str, PluginsPath)
    DUMP_LIBRARYPATH(str, QmlImportsPath)
    DUMP_LIBRARYPATH(str, ArchDataPath)
    DUMP_LIBRARYPATH(str, DataPath)
    DUMP_LIBRARYPATH(str, TranslationsPath)
    DUMP_LIBRARYPATH(str, ExamplesPath)
    DUMP_LIBRARYPATH(str, TestsPath)
    DUMP_LIBRARYPATH(str, SettingsPath)

    str << "\nStandard paths [*...* denote writable entry]:\n";
    DUMP_STANDARDPATH(str, DesktopLocation)
    DUMP_STANDARDPATH(str, DocumentsLocation)
    DUMP_STANDARDPATH(str, FontsLocation)
    DUMP_STANDARDPATH(str, ApplicationsLocation)
    DUMP_STANDARDPATH(str, MusicLocation)
    DUMP_STANDARDPATH(str, MoviesLocation)
    DUMP_STANDARDPATH(str, PicturesLocation)
    DUMP_STANDARDPATH(str, TempLocation)
    DUMP_STANDARDPATH(str, HomeLocation)
    DUMP_STANDARDPATH(str, AppLocalDataLocation)
    DUMP_STANDARDPATH(str, CacheLocation)
    DUMP_STANDARDPATH(str, GenericDataLocation)
    DUMP_STANDARDPATH(str, RuntimeLocation)
    DUMP_STANDARDPATH(str, ConfigLocation)
    DUMP_STANDARDPATH(str, DownloadLocation)
    DUMP_STANDARDPATH(str, GenericCacheLocation)
    DUMP_STANDARDPATH(str, GenericConfigLocation)
    DUMP_STANDARDPATH(str, AppDataLocation)
    DUMP_STANDARDPATH(str, AppConfigLocation)

    str << "\nFile selectors (increasing order of precedence):\n ";
    const QStringList allSelectors = QFileSelector().allSelectors();
    for (const QString &s : allSelectors)
        str << ' ' << s;

    str << "\n\nNetwork:\n  ";
#ifdef NETWORK_DIAG
#  ifndef QT_NO_SSL
    if (QSslSocket::supportsSsl()) {
        str << "Using \"" << QSslSocket::sslLibraryVersionString() << "\", version: 0x"
            << Qt::hex << QSslSocket::sslLibraryVersionNumber() << Qt::dec;
    } else {
        str << "\nSSL is not supported.";
    }
#  else // !QT_NO_SSL
    str << "SSL is not available.";
#  endif // QT_NO_SSL
#else
    str << "Qt Network module is not available.";
#endif // NETWORK_DIAG

    str << "\n\nPlatform capabilities:";
    DUMP_CAPABILITY(str, platformIntegration, ThreadedPixmaps)
    DUMP_CAPABILITY(str, platformIntegration, OpenGL)
    DUMP_CAPABILITY(str, platformIntegration, ThreadedOpenGL)
    DUMP_CAPABILITY(str, platformIntegration, SharedGraphicsCache)
    DUMP_CAPABILITY(str, platformIntegration, BufferQueueingOpenGL)
    DUMP_CAPABILITY(str, platformIntegration, WindowMasks)
    DUMP_CAPABILITY(str, platformIntegration, MultipleWindows)
    DUMP_CAPABILITY(str, platformIntegration, ApplicationState)
    DUMP_CAPABILITY(str, platformIntegration, ForeignWindows)
    DUMP_CAPABILITY(str, platformIntegration, NonFullScreenWindows)
    DUMP_CAPABILITY(str, platformIntegration, NativeWidgets)
    DUMP_CAPABILITY(str, platformIntegration, WindowManagement)
    DUMP_CAPABILITY(str, platformIntegration, SyncState)
    DUMP_CAPABILITY(str, platformIntegration, RasterGLSurface)
    DUMP_CAPABILITY(str, platformIntegration, AllGLFunctionsQueryable)
    DUMP_CAPABILITY(str, platformIntegration, ApplicationIcon)
    DUMP_CAPABILITY(str, platformIntegration, SwitchableWidgetComposition)
    str << '\n';

    const QStyleHints *styleHints = QGuiApplication::styleHints();
    const QChar passwordMaskCharacter = styleHints->passwordMaskCharacter();
    str << "\nStyle hints:\n  mouseDoubleClickInterval: " << styleHints->mouseDoubleClickInterval() << '\n'
        << "  mousePressAndHoldInterval: " << styleHints->mousePressAndHoldInterval() << '\n'
        << "  startDragDistance: " << styleHints->startDragDistance() << '\n'
        << "  startDragTime: " << styleHints->startDragTime() << '\n'
        << "  startDragVelocity: " << styleHints->startDragVelocity() << '\n'
        << "  keyboardInputInterval: " << styleHints->keyboardInputInterval() << '\n'
        //<< "  keyboardAutoRepeatRate: " << styleHints->keyboardAutoRepeatRate() << '\n'
        << "  cursorFlashTime: " << styleHints->cursorFlashTime() << '\n'
        << "  showIsFullScreen: " << styleHints->showIsFullScreen() << '\n'
        << "  showIsMaximized: " << styleHints->showIsMaximized() << '\n'
        << "  passwordMaskDelay: " << styleHints->passwordMaskDelay() << '\n'
        << "  passwordMaskCharacter: ";
    if (passwordMaskCharacter.unicode() >= 32 && passwordMaskCharacter.unicode() < 128)
        str << '\'' << passwordMaskCharacter << '\'';
    else
        str << "U+" << qSetFieldWidth(4) << qSetPadChar(u'0') << Qt::uppercasedigits << Qt::hex << passwordMaskCharacter.unicode() << Qt::dec << qSetFieldWidth(0);
    str << '\n'
        << "  fontSmoothingGamma: " << styleHints->fontSmoothingGamma() << '\n'
        << "  useRtlExtensions: " << styleHints->useRtlExtensions() << '\n'
        << "  setFocusOnTouchRelease: " << styleHints->setFocusOnTouchRelease() << '\n'
        << "  tabFocusBehavior: " << formatQDebug(styleHints->tabFocusBehavior()) << '\n'
        << "  singleClickActivation: " << styleHints->singleClickActivation() << '\n';
    str << "\nAdditional style hints (QPlatformIntegration):\n"
        << "  ReplayMousePressOutsidePopup: "
        << platformIntegration->styleHint(QPlatformIntegration::ReplayMousePressOutsidePopup).toBool() << '\n';

    const QPlatformTheme *platformTheme = QGuiApplicationPrivate::platformTheme();
    str << "\nTheme:"
           "\n  Platforms requested : " << platformIntegration->themeNames()
        << "\n            available : " << QPlatformThemeFactory::keys()
#ifdef QT_WIDGETS_LIB
        << "\n  Styles requested    : " << platformTheme->themeHint(QPlatformTheme::StyleNames).toStringList()
        << "\n         available    : " << QStyleFactory::keys()
#endif
           ;
    const QString iconTheme = platformTheme->themeHint(QPlatformTheme::SystemIconThemeName).toString();
    if (!iconTheme.isEmpty()) {
        str << "\n  Icon theme          : " << iconTheme
            << ", " << platformTheme->themeHint(QPlatformTheme::SystemIconFallbackThemeName).toString()
            << " from " << platformTheme->themeHint(QPlatformTheme::IconThemeSearchPaths).toStringList();
    }
    if (const QFont *systemFont = platformTheme->font())
        str << "\n  System font         : " << *systemFont<< '\n';

    if (platformTheme->usePlatformNativeDialog(QPlatformTheme::FileDialog))
        str << "  Native file dialog\n";
    if (platformTheme->usePlatformNativeDialog(QPlatformTheme::ColorDialog))
        str << "  Native color dialog\n";
    if (platformTheme->usePlatformNativeDialog(QPlatformTheme::FontDialog))
        str << "  Native font dialog\n";
    if (platformTheme->usePlatformNativeDialog(QPlatformTheme::MessageDialog))
        str << "  Native message dialog\n";

    str << "\nFonts:\n  General font : " << QFontDatabase::systemFont(QFontDatabase::GeneralFont) << '\n'
              << "  Fixed font   : " << QFontDatabase::systemFont(QFontDatabase::FixedFont) << '\n'
              << "  Title font   : " << QFontDatabase::systemFont(QFontDatabase::TitleFont) << '\n'
              << "  Smallest font: " << QFontDatabase::systemFont(QFontDatabase::SmallestReadableFont) << '\n';
    if (flags & QtDiagFonts) {
        const QStringList families = QFontDatabase::families();
        str << "\n  Families (" << families.size() << "):\n";
        for (int i = 0, count = int(families.size()); i < count; ++i)
            str << "    " << families.at(i) << '\n';

        const QList<int> standardSizes = QFontDatabase::standardSizes();
        str << "\n  Standard Sizes:";
        for (int i = 0, count = int(standardSizes.size()); i < count; ++i)
            str << ' ' << standardSizes.at(i);
        QList<QFontDatabase::WritingSystem> writingSystems = QFontDatabase::writingSystems();
        str << "\n\n  Writing systems:\n";
        for (int i = 0, count = int(writingSystems.size()); i < count; ++i)
            str << "    " << formatValueQDebug(writingSystems.at(i)) << '\n';
    }

    str << "\nPalette:\n" << QGuiApplication::palette();

    const QList<QScreen*> screens = QGuiApplication::screens();
    const int screenCount = int(screens.size());
    str << "\nScreens: " << screenCount << ", High DPI scaling: "
        << (QHighDpiScaling::isActive() ? "active" : "inactive") << '\n';
    for (int s = 0; s < screenCount; ++s) {
        const QScreen *screen = screens.at(s);
        const QPlatformScreen *platformScreen = screen->handle();
        const QRect geometry = screen->geometry();
        const QDpi dpi(screen->logicalDotsPerInchX(), screen->logicalDotsPerInchY());
        const QDpi nativeDpi = platformScreen->logicalDpi();
        const QRect nativeGeometry = platformScreen->geometry();
        str << '#' << ' ' << s << " \"" << screen->name() << '"'
                  << " Depth: " << screen->depth()
                  << " Primary: " <<  (screen == QGuiApplication::primaryScreen() ? "yes" : "no")
            << "\n  Manufacturer: " << screen->manufacturer()
            << "\n  Model: " << screen->model()
            << "\n  Serial number: " << screen->serialNumber()
            << "\n  Geometry: " << geometry;
        if (geometry != nativeGeometry)
            str << " (native: " << nativeGeometry << ')';
        str << " Available: " << screen->availableGeometry();
        if (geometry != screen->virtualGeometry())
            str << "\n  Virtual geometry: " << screen->virtualGeometry() << " Available: " << screen->availableVirtualGeometry();
        if (screen->virtualSiblings().size() > 1)
            str << "\n  " << screen->virtualSiblings().size() << " virtual siblings";
        str << "\n  Physical size: " << screen->physicalSize() << " mm"
            << "  Refresh: " << screen->refreshRate() << " Hz"
            << " Power state: " << platformScreen->powerState();
        str << "\n  Physical DPI: " << screen->physicalDotsPerInchX()
            << ',' << screen->physicalDotsPerInchY()
            << " Logical DPI: " << dpi;
        if (dpi != nativeDpi)
            str << " (native: " << nativeDpi << ')';
        str << ' ' << platformScreen->subpixelAntialiasingTypeHint() << "\n  ";
        if (QHighDpiScaling::isActive())
            str << "High DPI scaling factor: " << QHighDpiScaling::factor(screen) << ' ';
        str << "DevicePixelRatio: " << screen->devicePixelRatio();
        str << "\n  Primary orientation: " << screen->primaryOrientation()
            << " Orientation: " << screen->orientation()
            << " Native orientation: " << screen->nativeOrientation()
            << "\n\n";
    }

    const auto inputDevices = QInputDevice::devices();
    if (!inputDevices.isEmpty()) {
        str << "Input devices: " << inputDevices.size() << '\n';
        for (auto device : inputDevices) {
            str << "  " << formatValueQDebug(device->type())
                << " \"" << device->name() << "\",";
            if (!device->seatName().isEmpty())
                str << " seat: \"" << device->seatName() << '"';
            str << " capabilities:";
            const auto capabilities = device->capabilities();
            if (capabilities.testFlag(QInputDevice::Capability::Position))
                str << " Position";
            if (capabilities.testFlag(QInputDevice::Capability::Area))
                str << " Area";
            if (capabilities.testFlag(QInputDevice::Capability::Pressure))
                str << " Pressure";
            if (capabilities.testFlag(QInputDevice::Capability::Velocity))
                str << " Velocity";
            if (capabilities.testFlag(QInputDevice::Capability::NormalizedPosition))
                str << " NormalizedPosition";
            if (capabilities.testFlag(QInputDevice::Capability::MouseEmulation))
                str << " MouseEmulation";
            if (capabilities.testFlag(QInputDevice::Capability::Scroll))
                str << " Scroll";
            if (capabilities.testFlag(QInputDevice::Capability::Hover))
                str << " Hover";
            if (capabilities.testFlag(QInputDevice::Capability::Rotation))
                str << " Rotation";
            if (capabilities.testFlag(QInputDevice::Capability::XTilt))
                str << " XTilt";
            if (capabilities.testFlag(QInputDevice::Capability::YTilt))
                str << " YTilt";
            if (capabilities.testFlag(QInputDevice::Capability::TangentialPressure))
                str << " TangentialPressure";
            if (capabilities.testFlag(QInputDevice::Capability::ZPosition))
                str << " ZPosition";
            if (!device->availableVirtualGeometry().isNull()) {
                const auto r = device->availableVirtualGeometry();
                str << " availableVirtualGeometry: " << r.width() << 'x' << r.height()
                    << Qt::forcesign << r.x() << r.y() << Qt::noforcesign;
            }
            str << '\n';
        }
        str << "\n\n";
    }

#if !defined(QT_NO_OPENGL) && defined(Q_OS_LINUX)
    if (flags & QtDiagGl) {
        dumpGlInfo(str, flags & QtDiagGlExtensions);
        str << "\n";
    }
#else
    Q_UNUSED(flags);
#endif // !QT_NO_OPENGL

#if QT_CONFIG(vulkan) && defined(HAS_VULKAN)
    if (flags & QtDiagVk) {
        dumpVkInfo(str);
        str << "\n\n";
    }
#endif // vulkan

#ifdef Q_OS_WIN
    // On Windows, this will provide addition GPU info similar to the output of dxdiag.
    using QWindowsApplication = QNativeInterface::Private::QWindowsApplication;
    if (auto nativeWindowsApp = dynamic_cast<QWindowsApplication *>(QGuiApplicationPrivate::platformIntegration())) {
        const QVariant gpuInfoV = nativeWindowsApp->gpuList();
        if (gpuInfoV.typeId() == QMetaType::QVariantList) {
            const auto gpuList = gpuInfoV.toList();
            for (int i = 0; i < gpuList.size(); ++i) {
                const QString description =
                        gpuList.at(i).toMap().value(QStringLiteral("printable")).toString();
                if (!description.isEmpty())
                    str << "\nGPU #" << (i + 1) << ":\n" << description << '\n';
            }
            str << "\n";
        }
    }
#endif // Q_OS_WIN

    if (flags & QtDiagRhi) {
        dumpRhiInfo(str);
        str << "\n";
    }

    return result;
}

QT_END_NAMESPACE
