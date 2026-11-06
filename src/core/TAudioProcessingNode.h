/*
    Copyright (C) 2010 - 2026 Remon Sijrier
    This file is part of Traverso
*/

#pragma once

#include "TCommand.h"
#include "TContextItem.h"

#include <QPointer>
#include <QPropertyAnimation>

class TAudioPlugin;
class TAudioPluginChain;
class TSession;
class GainEnvelope;

// The audio of a clip or a track: gain, mute, pan and the plugin chain. A
// member of its owner (audio()); it knows no clips, tracks or views
class TAudioProcessingNode : public QObject
{
    Q_OBJECT
    Q_PROPERTY(float gain READ get_gain WRITE set_gain NOTIFY gainChanged)

public:
    // owner: the clip or track this is the audio of, and the parent of the
    // plugin chain; session: the chain's, when the owner knows it already
    explicit TAudioProcessingNode(TSession* session, TContextItem* owner);
    ~TAudioProcessingNode() override {}

    TCommand* add_plugin(TAudioPlugin* plugin);
    TCommand* remove_plugin(TAudioPlugin* plugin);

    TAudioPluginChain* get_plugin_chain() const {return m_pluginChain;}
    // The gain of the chain, also in the audio thread
    GainEnvelope* get_fader() const T_REALTIME_SAFE {return m_fader;}
    float get_pan() const {return m_pan;}

    void set_muted(bool muted);
    void set_pan(float pan);

    bool is_muted() const {return m_isMuted;}

private:
    // Made with the node and never replaced: the audio thread reads them
    TAudioPluginChain* const      m_pluginChain;
    GainEnvelope* const           m_fader;
    bool                          m_isMuted;
    float                         m_pan;
    QPointer<QPropertyAnimation>  m_gainAnimation;

public slots:
    float get_gain() T_REALTIME_SAFE;
    QString get_gain_db_string(int decimals=1);

    void set_gain(float gain);
    void set_gain_animated(float gain);
    TCommand* mute();

signals:
    // One signal per fact; what follows from several (being heard, the
    // state shown) is the owner's
    void muteChanged(bool isMuted);
    void panChanged();
    void gainChanged();
};
