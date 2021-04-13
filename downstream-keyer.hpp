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
	QLineEdit *nameEdit;
	QListWidget *scenesList;
	QComboBox *transitionList;
	QSpinBox *transitionDuration;
	QLabel * transitionDurationLabel;
	QToolBar * scenesToolbar;

	static void source_rename(void *data, calldata_t *calldata);
	static void source_remove(void *data, calldata_t *calldata);

	void ChangeSceneIndex(bool relative, int idx, int invalidIdx);
private slots:
	void on_actionAddScene_triggered();
	void on_actionRemoveScene_triggered();
	void on_actionSceneUp_triggered();
	void on_actionSceneDown_triggered();
	void on_actionSceneNull_triggered();
	void on_scenesList_currentItemChanged(QListWidgetItem *current,
					      QListWidgetItem *prev);
	void on_transitionList_currentIndexChanged(int);
signals:

public:
	DownstreamKeyer(int channel);
	~DownstreamKeyer();

	void Save(obs_data_t *data);
	void Load(obs_data_t *data);
	void UpdateTransitions();
};
