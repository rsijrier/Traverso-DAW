#pragma once

#include <QString>
#include <QStringList>

class TAudioBusConfiguration
{
public:
    TAudioBusConfiguration();

    QString name;
    QStringList channelNames;
    QString type;
    QString bustype;
    int channelcount;
    bool isInternalBus;
};

