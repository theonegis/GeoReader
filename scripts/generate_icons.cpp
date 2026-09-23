// Deterministic platform icon compiler. The original vector artwork is shared;
// platform geometry and all raster sizes are generated together, never by hand.
#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QSvgRenderer>
#include <QTextStream>
#include <cmath>
#include <stdexcept>

static void write(const QString &path, const QByteArray &data) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
    throw std::runtime_error(qPrintable("Cannot write " + path));
}
static QByteArray png(const QImage &image) {
  QByteArray data;
  QBuffer buffer(&data);
  if (!image.save(&buffer, "PNG")) throw std::runtime_error("PNG encoder failed");
  return data;
}
static QImage render(const QByteArray &svg, int size) {
  QSvgRenderer renderer(svg);
  if (!renderer.isValid()) throw std::runtime_error("Invalid icon SVG");
  // Supersample each target directly from the vector, never upscale a small icon.
  QImage image(size * 4, size * 4, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  renderer.render(&painter);
  painter.end();
  return image.scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}
static QRect opaqueBounds(const QImage &image) {
  QRect bounds;
  for (int y=0;y<image.height();++y)
    for (int x=0;x<image.width();++x)
      if (qAlpha(image.pixel(x,y)) >= 128) bounds |= QRect(x,y,1,1);
  return bounds;
}
int main(int argc, char **argv) {
  QGuiApplication app(argc, argv);
  try {
    if (argc != 2) throw std::runtime_error("usage: GeoReaderIconTool <project-root>");
    const QString root=QDir(argv[1]).absolutePath();
    const QString directory=root+"/resources/icons/";
    QFile masterFile(directory+"georeader-artwork.svg");
    if (!masterFile.open(QIODevice::ReadOnly)) throw std::runtime_error("Master SVG missing");
    const auto master=masterFile.readAll();
    const QList<int> sizes{16,20,24,30,32,36,40,48,60,64,72,80,96,128,256,512,1024};
    QMap<QString,QMap<int,QImage>> images;
    for (const auto &platform : {QString("macos"),QString("windows"),QString("linux")}) {
      // Product keylines, not a claim that each OS mandates one universal ratio.
      // macOS legacy ICNS needs its own transparent margin; modern Win32/Linux
      // keep a slightly larger plate for small taskbar/menu sizes.
      const double plate=platform=="macos"?103.:104.;
      const double scale=plate/112.;
      QByteArray svg=master;
      const QByteArray radius=platform=="macos"?"25":platform=="windows"?"10":"18";
      svg.replace("rx=\"18\"", "rx=\""+radius+"\"");
      svg.replace("  <rect",QString("  <g transform=\"translate(%1 %1) scale(%2)\">\n  <rect")
                      .arg((128.-128.*scale)/2.,0,'f',9).arg(scale,0,'f',9).toUtf8());
      svg.replace("</svg>","  </g>\n</svg>");
      write(directory+platform+"/georeader.svg",svg);
      if(platform=="linux") write(directory+"georeader.svg",svg);
      for(int size:sizes) {
        auto image=render(svg,size);
        auto bounds=opaqueBounds(image);
        const int expected=qRound(size*plate/128.);
        if (std::abs(bounds.width()-expected)>1 || std::abs(bounds.height()-expected)>1 ||
            std::abs(bounds.left()-(size-bounds.right()-1))>1 ||
            std::abs(bounds.top()-(size-bounds.bottom()-1))>1 ||
            qAlpha(image.pixel(0,0))!=0)
          throw std::runtime_error("Icon keyline/centering/alpha regression");
        images[platform][size]=image;
        write(directory+platform+"/"+QString::number(size)+".png",png(image));
      }
      auto bounds=opaqueBounds(images[platform][1024]);
      QTextStream(stdout)<<platform<<": 1024 canvas, plate "<<bounds.width()<<" x "
                         <<bounds.height()<<", centered, transparent; "<<sizes.size()<<" sizes verified\n";
    }
    // Windows ICO: PNG-backed 32-bit RGBA entries (supported by Vista and newer).
    const QList<int> icoSizes{16,20,24,30,32,36,40,48,60,64,72,80,96,128,256};
    QByteArray ico;
    QDataStream out(&ico,QIODevice::WriteOnly);out.setByteOrder(QDataStream::LittleEndian);
    out<<quint16(0)<<quint16(1)<<quint16(icoSizes.size());
    quint32 offset=6+16*icoSizes.size();
    QList<QByteArray> payloads;
    for(int size:icoSizes){
      const auto data=png(images["windows"][size]);payloads<<data;
      out<<quint8(size==256?0:size)<<quint8(size==256?0:size)<<quint8(0)<<quint8(0)
         <<quint16(1)<<quint16(32)<<quint32(data.size())<<offset;
      offset+=data.size();
    }
    for(const auto &data:payloads)out.writeRawData(data.constData(),data.size());
    write(directory+"georeader.ico",ico);
    // Legacy ICNS includes 1x and Retina representations. Keep this compatible
    // with Qt bundles; no Icon Composer or newer macOS-only format is required.
    QByteArray chunks;
    QDataStream chunkStream(&chunks,QIODevice::WriteOnly);chunkStream.setByteOrder(QDataStream::BigEndian);
    const QList<QPair<QByteArray,int>> icnsSizes{{"icp4",16},{"icp5",32},{"icp6",64},
      {"ic07",128},{"ic08",256},{"ic09",512},{"ic10",1024},{"ic11",32},{"ic12",64},
      {"ic13",256},{"ic14",512}};
    for(const auto &entry:icnsSizes){
      const auto data=png(images["macos"][entry.second]);
      chunkStream.writeRawData(entry.first.constData(),4);chunkStream<<quint32(data.size()+8);
      chunkStream.writeRawData(data.constData(),data.size());
    }
    QByteArray icns;QDataStream icnsStream(&icns,QIODevice::WriteOnly);icnsStream.setByteOrder(QDataStream::BigEndian);
    icnsStream.writeRawData("icns",4);icnsStream<<quint32(chunks.size()+8);icnsStream.writeRawData(chunks.constData(),chunks.size());
    write(directory+"georeader.icns",icns);
    // Contact sheet: actual pixels on light and dark backgrounds, including
    // the original artwork to make the optical size correction reviewable.
    QImage sheet(880,450,QImage::Format_ARGB32_Premultiplied);sheet.fill(Qt::white);
    QPainter painter(&sheet);painter.setFont(QFont("Arial",13));
    for(int theme=0;theme<2;++theme){
      const int y=theme*225;painter.fillRect(0,y,880,225,theme?QColor("#20242a"):QColor("#edf0f4"));
      painter.setPen(theme?Qt::white:Qt::black);
      const QStringList names{"Original", "macOS", "Windows", "Linux"};
      for(int column=0;column<4;++column){
        const int x=column*220;
        painter.drawText(QRect(x,y+12,220,24),Qt::AlignCenter,names[column]);
        auto icon=column==0?render(master,128):images[column==1?"macos":column==2?"windows":"linux"][128];
        painter.drawImage(x+46,y+44,icon);
        const auto svgPlatform=column==1?"macos":column==2?"windows":"linux";
        painter.drawImage(x+52,y+184,column==0?render(master,16):images[svgPlatform][16]);
        painter.drawImage(x+91,y+180,column==0?render(master,24):images[svgPlatform][24]);
        painter.drawImage(x+135,y+176,column==0?render(master,32):images[svgPlatform][32]);
      }
    }
    painter.end();write(root+"/tests/output/icon-comparison.png",png(sheet));
    QTextStream(stdout)<<"PASS: ICO 15 representations, ICNS 11 representations, platform assets generated\n";
    return 0;
  }catch(const std::exception &e){QTextStream(stderr)<<e.what()<<"\n";return 1;}
}
