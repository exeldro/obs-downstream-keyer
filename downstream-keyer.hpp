#pragma once

#include <qcombobox.h>
#include <qlistwidget.h>
#include <qspinbox.h>
#include <QWidget>

#include "obs.h"

class DownstreamKeyer : public QWidget {
	Q_OBJECT

private:
	int outputChannel;
	QLineEdit *name;
	QListWidget * scenesList;
	obs_source_t *transition;
	QComboBox * transitionList;
	QSpinBox * transitionDuration;

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
};
