#pragma once

#include <qcombobox.h>
#include <qlabel.h>
#include <qlistwidget.h>
#include <qspinbox.h>
#include <qtoolbar.h>
#include <QWidget>

#include "obs.h"

class DownstreamKeyer : public QWidget {
	Q_OBJECT

private:
	int outputChannel;
	obs_source_t *transition;
	QListWidget *scenesList;
	QToolBar *scenesToolbar;
	uint32_t transitionDuration;

	static void source_rename(void *data, calldata_t *calldata);
	static void source_remove(void *data, calldata_t *calldata);
	static bool enable_DSK_hotkey(void *data, obs_hotkey_pair_id id,
				      obs_hotkey_t *hotkey, bool pressed);
	static bool disable_DSK_hotkey(void *data, obs_hotkey_pair_id id,
				       obs_hotkey_t *hotkey, bool pressed);

	void ChangeSceneIndex(bool relative, int idx, int invalidIdx);

private slots:
	void on_actionAddScene_triggered();
	void on_actionRemoveScene_triggered();
	void on_actionSceneUp_triggered();
	void on_actionSceneDown_triggered();
	void on_actionSceneNull_triggered();
	void on_scenesList_itemSelectionChanged();
signals:

public:
	DownstreamKeyer(int channel);
	~DownstreamKeyer();

	void Save(obs_data_t *data);
	void Load(obs_data_t *data);
	void SetTransition(const char *transition_name);
	std::string GetTransition();
	void SetTransitionDuration(int duration);
	int GetTransitionDuration();
};
