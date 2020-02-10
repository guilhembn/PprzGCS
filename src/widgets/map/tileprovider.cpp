#include "tileprovider.h"
#include "math.h"
#include <iostream>
#include <string>
#include <fstream>
#include <QMap>
#include <QStandardPaths>
#include <QNetworkReply>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QNetworkProxy>
#include <QGraphicsScene>


static const char tilesPath[] = "/home/fabien/DEV/test_qt/PprzGCS/data/map";

TileProvider::TileProvider(std::unique_ptr<TileProviderConfig>& config, int z, int displaySize, QObject *parent) : QObject (parent),
    config(config), _zoomLevel(16), z_value(z), tileDisplaySize(displaySize)
{
    if(tileDisplaySize == 0) {
        tileDisplaySize = config->tileSize;
    }
    motherTile = new TileItem(nullptr, tileDisplaySize);
    manager = new QNetworkAccessManager(this);
    diskCache = new QNetworkDiskCache(this);
    diskCache->setCacheDirectory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
    manager->setCache(diskCache);

    connect(manager, &QNetworkAccessManager::finished, this, &TileProvider::handleReply);
}

std::string TileProvider::tilePath(Point2DTile coor) {
    std::string path = std::string(tilesPath) + "/" + config->dir.toStdString()+ "/" +
        std::to_string(coor.zoom()) +
        "/X" + std::to_string(coor.xi()) +
        "_Y" + std::to_string(coor.yi()) + config->format.toStdString();

    return path;
}

QUrl TileProvider::tileUrl(Point2DTile coor) {
    char plop[300];
    int args[3];
    args[config->posX] = coor.xi();
    args[config->posY] = coor.yi();
    args[config->posZoom] = coor.zoom();
    snprintf(plop, 99, config->addr.toStdString().c_str(), args[0], args[1], args[2]);
    return QUrl(plop);
}

void TileProvider::fetch_tile(Point2DTile t, Point2DTile tObj) {
    if(t.isValid() && tObj.isValid()) {
        TileItem* tile = getTile(t);
        TileItem* tileObj = getTile(tObj);
        if(!tile->dataGood()) {

            // try to load the tile
            if(load_tile_from_disk(tile)) {
                if(/*!tileObj->hasData() && */tile != tileObj) {    // an ancestor was loaded. inherit its data for tileObj
                    tileObj->setInheritedData();
                }
                emit(displayTile(tile, tileObj));
            } else {
                // tile not on disk, try to load ancestors then direct childs
                TileItem* current = tile->mother();

                // fist, load ancestors
                while(current != nullptr) {
                    // tile found on disk
                    if(load_tile_from_disk(current)) {
                        // ancestor found
                        tileObj->setInheritedData();
                        emit(displayTile(current, tileObj));
                        break;
                    } else {
                        // this tile was not on the disk, so try with its mother
                        current = current->mother();
                    }
                }
                // second, load direct childs
                for(int i=0; i<2; i++) {
                    for(int j=0; j<2; j++) {
                        Point2DTile childPoint = tile->coordinates().childPoint(i, j);
                        TileItem* child = getTile(childPoint);
                        if(load_tile_from_disk(child)) {
                            tileObj->setInheritedData();
                            emit(displayTile(child, tileObj));
                        }
                    }
                }

                // now, dl the right tile
                QUrl url = tileUrl(t);
                QNetworkRequest request = QNetworkRequest(url);


                // tuple : what is dl, what we want to display
                //downloading.append(std::make_tuple(tile, tile));
                request.setRawHeader("User-Agent", "Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:72.0) Gecko");
                QList<QVariant> l = QList<QVariant>();
                l.push_back(QVariant::fromValue(tile));
                l.push_back(QVariant::fromValue(tile));
                request.setAttribute(QNetworkRequest::User, l);
                manager->get(request);

            }

        } else {
            //tile with data found in tree
            emit(displayTile(tile, tile));
        }

    }
}

void TileProvider::handleReply(QNetworkReply *reply) {
    QList<QVariant> l = reply->request().attribute(QNetworkRequest::User).toList();

    TileItem* tileCur = l.takeFirst().value<TileItem*>();
    TileItem* tileObj = l.takeFirst().value<TileItem*>();

    if(reply->error() == QNetworkReply::NetworkError::NoError) {
        std::string path = tilePath(tileCur->coordinates());
        QFile file(path.c_str());
        QFileInfo fi(path.c_str());
        QDir dirName = fi.dir();
        if(!dirName.exists()) {
            dirName.mkpath(dirName.path());
        }

        if(file.open(QIODevice::WriteOnly)) {
            file.write(reply->readAll());
            file.close();
            reply->deleteLater();

            if(load_tile_from_disk(tileCur)) {
                if(/*!tileObj->hasData() && */tileCur != tileObj) {
                    // an ancestor was loaded. inherit its data for tileObj
                    tileObj->setInheritedData();
                }
                emit(displayTile(tileCur, tileObj));
            } else {
                std::cout << "Image just saved, but it could not be loaded!" << std::endl;
            }
        } else {
            std::cout << "Could not save image on the disk!" << std::endl;
        }

    } else {
        //tile dl failed. try the parent tile ?
        TileItem* parentTile = tileCur->mother();
        if(parentTile != nullptr) {
            fetch_tile(tileCur->mother()->coordinates(), tileObj->coordinates());
        }

        std::cout << "Error " << reply->error() << " ! " << reply->readAll().toStdString() << std::endl;
    }

}


bool TileProvider::load_tile_from_disk(TileItem* item) {
    std::string path = tilePath(item->coordinates());
    std::ifstream f(path);
    if(f.good()) {
        // tile found on disk
        QPixmap pixmap(path.c_str());
        item->setPixmap(pixmap.scaled(tileDisplaySize, tileDisplaySize));
        return true;
    } else {
        return false;
    }
}

void TileProvider::setZoomLevel(int z) {
    if(z == _zoomLevel) {
        return; // nothing change
    }

    if(z > config->zoomMax) {
        _zoomLevel = config->zoomMax;
    } else if(z < config->zoomMin) {
        _zoomLevel = config->zoomMin;
    } else {
        _zoomLevel = z;
    }

    //TODO improve iterator usability (make a C++ standard one)
    TileIterator iter(motherTile);
    while(true) {
        TileItem* tile = iter.next();
        if(tile == nullptr) {
            break;
        }
        if(tile->hasData() && tile->coordinates().zoom() != _zoomLevel) {
            tile->hide();
        }
    }

}

TileItem* TileProvider::getTile(Point2DTile p) {
    TileItem* current = motherTile;

    // mask to apply to the full path (to the objective tile 'p') to get the partial path (the 'next' tile)
    int mask = 0;

    for(int i=p.zoom()-1; i>=0; i--) {
        int xi = (p.xi() & 1<<i) ? 1 : 0;
        int yi = (p.yi() & 1<<i) ? 1 : 0;
        mask |= 1<<i;

        TileItem* next = current->child(xi, yi);

        if(next == nullptr) {
            int x = (p.xi() & mask) >> i;
            int y = (p.yi() & mask) >> i;
            int zoom = p.zoom()-i;

            next = new TileItem(current, motherTile->tileSize(), Point2DTile(x, y, zoom));
            current->setChild(next, xi, yi);
        }

        current = next;
    }

    return current;
}
