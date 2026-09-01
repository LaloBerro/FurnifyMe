// Writes assets/icon.ico from IconSet::appIcon() - the SAME painting the
// running window puts in its title bar, so the executable's mark and the
// application's mark cannot drift apart.
//
// Built only under -DFURNIFYME_BUILD_ICON_TOOL=ON, and run by hand when the
// mark changes:
//
//   cmake --preset windows -DFURNIFYME_BUILD_ICON_TOOL=ON
//   cmake --build --preset windows --target make_icon
//   .\build\RelWithDebInfo\make_icon.exe assets\icon.ico
//
// The .ico it writes is COMMITTED, rather than generated during every build.
// The icon is explicitly a placeholder, a committed asset needs no host tool
// in the build graph, and a resource compiler wants a real file on disk at
// configure time. If the mark ever becomes permanent, moving this into a
// custom command is a small change from here.
//
// Qt cannot be relied on to WRITE .ico (the handler is an optional image
// plugin), so the container is assembled here. It is a small format: a
// 6-byte header, one 16-byte directory entry per size, then the images.
//
// The 256px image is a PNG blob and every smaller one is a classic DIB, which
// is not belt-and-braces. Windows itself has read PNG entries at any size
// since Vista, but GDI+ - System.Drawing.Icon, and so a good deal of tooling
// that inspects icons - still cannot, and an all-PNG file written here failed
// to load in exactly that way on the first attempt. A DIB at 256 would cost
// 256 KB on its own, which is what PNG is there for.
#include "IconSet.h"
#include "Theme.h"

#include <QApplication>
#include <QBuffer>
#include <QByteArray>
#include <QDataStream>
#include <QFile>
#include <QImage>
#include <QPixmap>
#include <QVector>

#include <cstdio>

namespace {

// 256 is written as 0 in a directory entry - the field is one byte and 256
// does not fit in it.
const QVector<int> kSizes{16, 24, 32, 48, 64, 128, 256};

// One icon image in the ICONDIR's own terms: a BITMAPINFOHEADER, the pixels
// BOTTOM-UP in BGRA, then the 1bpp AND mask the format still requires even at
// 32 bits per pixel. The mask is left all zeros - "take every pixel from the
// image" - because the alpha channel is what actually carries the transparency
// on any Windows that has existed for twenty years, and a mask derived from
// the alpha would only matter to a renderer that ignores the alpha anyway.
QByteArray encodeDib(const QImage& source)
{
    const QImage image = source.convertToFormat(QImage::Format_ARGB32);
    const int w = image.width();
    const int h = image.height();

    QByteArray blob;
    QBuffer buffer(&blob);
    buffer.open(QIODevice::WriteOnly);
    QDataStream out(&buffer);
    out.setByteOrder(QDataStream::LittleEndian);

    // The AND mask's rows are padded to four bytes, like every DIB row.
    const int maskStride = ((w + 31) / 32) * 4;

    out << quint32(40) << qint32(w)
        // DOUBLE height: the header describes the image and the mask as one
        // stacked bitmap. Writing h here is the single most common way to get
        // a hand-built .ico half right, and it shows up as an icon drawn from
        // its own bottom half.
        << qint32(2 * h)
        << quint16(1) << quint16(32) << quint32(0)
        << quint32(w * h * 4 + maskStride * h)
        << qint32(0) << qint32(0) << quint32(0) << quint32(0);

    for (int y = h - 1; y >= 0; --y) {
        for (int x = 0; x < w; ++x) {
            const QRgb pixel = image.pixel(x, y);
            out << quint8(qBlue(pixel)) << quint8(qGreen(pixel))
                << quint8(qRed(pixel)) << quint8(qAlpha(pixel));
        }
    }
    for (int y = 0; y < h; ++y) {
        for (int i = 0; i < maskStride; ++i) out << quint8(0);
    }
    return blob;
}

}  // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    // The mark is painted from the theme's tokens, so the palette has to be
    // installed before a single pixel is asked for. Graphite, always: this
    // writes the file the app SHIPS with, never the palette some developer
    // happens to have edited into their own settings.
    Theme::apply(app);
    Theme::setSpec(Theme::defaultSpec());

    const QString path = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                  : QStringLiteral("assets/icon.ico");

    QVector<QByteArray> images;
    for (int size : kSizes) {
        const QImage painted = IconSet::appIconPixmap(size).toImage();
        if (size >= 256) {
            QByteArray png;
            QBuffer buffer(&png);
            buffer.open(QIODevice::WriteOnly);
            if (!painted.save(&buffer, "PNG")) {
                std::fprintf(stderr, "could not encode the %dpx image\n", size);
                return 1;
            }
            images.push_back(png);
        } else {
            images.push_back(encodeDib(painted));
        }
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::fprintf(stderr, "could not open %s for writing\n", qPrintable(path));
        return 1;
    }

    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);
    out << quint16(0) << quint16(1) << quint16(kSizes.size());   // reserved, type ICO, count

    // The images follow the whole directory, so the first offset is past all
    // of it - derived rather than written out, so adding a size cannot leave a
    // stale constant behind.
    quint32 offset = 6 + 16 * quint32(kSizes.size());
    for (int i = 0; i < kSizes.size(); ++i) {
        const int size = kSizes[i];
        out << quint8(size >= 256 ? 0 : size)     // width, 0 meaning 256
            << quint8(size >= 256 ? 0 : size)     // height, likewise
            << quint8(0)                          // palette size: none, it is truecolour
            << quint8(0)                          // reserved
            << quint16(1)                         // colour planes
            << quint16(32)                        // bits per pixel
            << quint32(images[i].size())
            << offset;
        offset += quint32(images[i].size());
    }
    for (const QByteArray& png : images) out.writeRawData(png.constData(), png.size());
    if (!file.flush()) {
        std::fprintf(stderr, "could not write %s\n", qPrintable(path));
        return 1;
    }

    std::printf("wrote %s (%d images, %lld bytes)\n", qPrintable(path),
                int(kSizes.size()), file.size());
    return 0;
}
