#include "downstream-keyer.hpp"

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSpinBox>
#include <QToolBar>
#include <QVBoxLayout>
#include <../UI/obs-frontend-api/obs-frontend-api.h>

#define QT_UTF8(str) QString::fromUtf8(str)
#define QT_TO_UTF8(str) str.toUtf8().constData()

DownstreamKeyer::DownstreamKeyer(int channel)
	: outputChannel(channel), transition(nullptr)
{
	auto layout = new QVBoxLayout(this);
	layout->setSpacing(0);
	layout->setContentsMargins(0, 0, 0, 0);

	nameEdit = new QLineEdit();
	nameEdit->setObjectName(QStringLiteral("name"));
	nameEdit->setText("Downstream Keyer 1");
	//name->setMinimumSize(QSize(0, 22));
	//name->setMaximumSize(QSize(16777215, 22));
	layout->addWidget(nameEdit);

	transitionList = new QComboBox();

	connect(transitionList, SIGNAL(currentIndexChanged(int)), this,
		SLOT(on_transitionList_currentIndexChanged(int)));

	layout->addWidget(transitionList);

	auto horizontalLayout = new QHBoxLayout();
	horizontalLayout->setSpacing(4);
	horizontalLayout->setObjectName(QStringLiteral("horizontalLayout_3"));
	transitionDurationLabel = new QLabel(this);
	transitionDurationLabel->setObjectName(
		QStringLiteral("transitionDurationLabel"));
	transitionDurationLabel->setText("Duration");

	horizontalLayout->addWidget(transitionDurationLabel);

	transitionDuration = new QSpinBox(this);
	transitionDuration->setObjectName(QStringLiteral("transitionDuration"));
	transitionDuration->setMinimum(50);
	transitionDuration->setMaximum(20000);
	transitionDuration->setSingleStep(50);
	transitionDuration->setValue(300);

	horizontalLayout->addWidget(transitionDuration);

	transitionDurationLabel->setBuddy(transitionDuration);

	layout->addLayout(horizontalLayout);

	scenesList = new QListWidget(this);
	scenesList->setObjectName(QStringLiteral("scenes"));
	QSizePolicy sizePolicy6(QSizePolicy::Preferred, QSizePolicy::Expanding);
	sizePolicy6.setHorizontalStretch(0);
	sizePolicy6.setVerticalStretch(0);
	sizePolicy6.setHeightForWidth(
		scenesList->sizePolicy().hasHeightForWidth());
	scenesList->setSizePolicy(sizePolicy6);
	scenesList->setContextMenuPolicy(Qt::CustomContextMenu);
	scenesList->setFrameShape(QFrame::NoFrame);
	scenesList->setFrameShadow(QFrame::Plain);
	scenesList->setProperty("showDropIndicator", QVariant(true));
	scenesList->setDragEnabled(true);
	scenesList->setDragDropMode(QAbstractItemView::InternalMove);
	scenesList->setDefaultDropAction(Qt::TargetMoveAction);
	connect(scenesList,
		SIGNAL(currentItemChanged(QListWidgetItem *,
					  QListWidgetItem *)),
		this,
		SLOT(on_scenesList_currentItemChanged(QListWidgetItem *,
						      QListWidgetItem *)));

	layout->addWidget(scenesList);

	scenesToolbar = new QToolBar(this);
	scenesToolbar->setObjectName(QStringLiteral("scenesToolbar"));
	scenesToolbar->setIconSize(QSize(16, 16));
	scenesToolbar->setFloatable(false);

	auto actionAddScene = new QAction(this);
	actionAddScene->setObjectName(QStringLiteral("actionAddScene"));
	actionAddScene->setProperty("themeID", "addIconSmall");
	actionAddScene->setText("Add");
	connect(actionAddScene, SIGNAL(triggered()), this,
		SLOT(on_actionAddScene_triggered()));
	scenesToolbar->addAction(actionAddScene);

	auto actionRemoveScene = new QAction(this);
	actionRemoveScene->setObjectName(QStringLiteral("actionRemoveScene"));
	actionRemoveScene->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	actionRemoveScene->setProperty("themeID", "removeIconSmall");
	actionRemoveScene->setText("Remove");
	connect(actionRemoveScene, SIGNAL(triggered()), this,
		SLOT(on_actionRemoveScene_triggered()));
	scenesToolbar->addAction(actionRemoveScene);

	scenesToolbar->addSeparator();

	auto actionSceneUp = new QAction(this);
	actionSceneUp->setObjectName(QStringLiteral("actionSceneUp"));
	actionSceneUp->setProperty("themeID", "upArrowIconSmall");
	actionSceneUp->setText("Move up");
	connect(actionSceneUp, SIGNAL(triggered()), this,
		SLOT(on_actionSceneUp_triggered()));
	scenesToolbar->addAction(actionSceneUp);

	auto actionSceneDown = new QAction(this);
	actionSceneDown->setObjectName(QStringLiteral("actionSceneDown"));
	actionSceneDown->setProperty("themeID", "downArrowIconSmall");
	actionSceneDown->setText("Move down");
	connect(actionSceneDown, SIGNAL(triggered()), this,
		SLOT(on_actionSceneDown_triggered()));
	scenesToolbar->addAction(actionSceneDown);

	scenesToolbar->addSeparator();

	auto actionSceneNull = new QAction(this);
	actionSceneNull->setObjectName(QStringLiteral("actionSceneNull"));
	actionSceneNull->setProperty("themeID", "pauseIconSmall");
	actionSceneNull->setText("No scene");
	connect(actionSceneNull, SIGNAL(triggered()), this,
		SLOT(on_actionSceneNull_triggered()));
	scenesToolbar->addAction(actionSceneNull);

	// Themes need the QAction dynamic properties
	for (QAction *x : scenesToolbar->actions()) {
		QWidget *temp = scenesToolbar->widgetForAction(x);

		for (QByteArray &y : x->dynamicPropertyNames()) {
			temp->setProperty(y, x->property(y));
		}
	}

	layout->addWidget(scenesToolbar);

	layout->addItem(new QSpacerItem(150, 0, QSizePolicy::Fixed,
					QSizePolicy::Minimum));

	const auto sh = obs_get_signal_handler();
	signal_handler_connect(sh, "source_rename", source_rename, this);
	signal_handler_connect(sh, "source_remove", source_remove, this);

	setLayout(layout);
}

DownstreamKeyer::~DownstreamKeyer()
{
	obs_set_output_source(outputChannel, nullptr);
	if (transition) {
		obs_transition_clear(transition);
		obs_source_release(transition);
	}
	while (scenesList->count()) {
		const auto item = scenesList->item(0);
		scenesList->removeItemWidget(item);
		delete item;
	}
	delete scenesList;
	delete transitionList;
	delete nameEdit;
	delete transitionDuration;
	delete transitionDurationLabel;
	delete scenesToolbar;
}

void DownstreamKeyer::on_actionAddScene_triggered()
{

	obs_source_t *scene = obs_frontend_preview_program_mode_active()
				      ? obs_frontend_get_current_preview_scene()
				      : obs_frontend_get_current_scene();
	auto sceneName = QT_UTF8(obs_source_get_name(scene));
	if (scenesList->findItems(sceneName, Qt::MatchFixedString).count() ==
	    0) {
		scenesList->addItem(sceneName);
	}

	obs_source_release(scene);
}

void DownstreamKeyer::on_actionRemoveScene_triggered()
{
	auto item = scenesList->currentItem();
	if (!item)
		return;
	scenesList->removeItemWidget(item);
	delete item;
}

void DownstreamKeyer::on_actionSceneUp_triggered()
{
	ChangeSceneIndex(true, -1, 0);
}

void DownstreamKeyer::on_actionSceneDown_triggered()
{
	ChangeSceneIndex(true, 1, scenesList->count() - 1);
}

void DownstreamKeyer::on_actionSceneNull_triggered()
{
	scenesList->setCurrentRow(-1);
}

void DownstreamKeyer::on_scenesList_currentItemChanged(QListWidgetItem *current,
						       QListWidgetItem *prev)
{
	auto currentSource =
		current ? obs_get_source_by_name(QT_TO_UTF8(current->text()))
			: nullptr;
	if (transition) {
		auto prevSource =
			prev ? obs_get_source_by_name(QT_TO_UTF8(prev->text()))
			     : nullptr;
		obs_transition_set(transition, prevSource);
		obs_transition_start(transition, OBS_TRANSITION_MODE_AUTO,
				     transitionDuration->value(),
				     currentSource);

		if (obs_get_output_source(outputChannel) != transition) {
			obs_set_output_source(outputChannel, transition);
		}
	} else {
		obs_set_output_source(outputChannel, currentSource);
	}
	obs_source_release(currentSource);
}

void DownstreamKeyer::ChangeSceneIndex(bool relative, int offset,
				       int invalidIdx)
{
	int idx = scenesList->currentRow();
	if (idx == -1 || idx == invalidIdx)
		return;

	scenesList->blockSignals(true);
	QListWidgetItem *item = scenesList->takeItem(idx);

	if (!relative)
		idx = 0;

	scenesList->insertItem(idx + offset, item);
	scenesList->setCurrentRow(idx + offset);
	item->setSelected(true);
	scenesList->blockSignals(false);
}

void DownstreamKeyer::on_transitionList_currentIndexChanged(int)
{
	auto oldTransitionName = obs_source_get_name(transition);
	obs_source_t *oldTransition = transition;
	obs_source_t *newTransition = nullptr;
	if (!transitionList->currentText().isEmpty()) {
		obs_frontend_source_list transitions = {0};
		obs_frontend_get_transitions(&transitions);
		for (size_t i = 0; i < transitions.sources.num; i++) {
			const char *n = obs_source_get_name(
				transitions.sources.array[i]);
			if (!n)
				continue;
			if (strcmp(n,
				   QT_TO_UTF8(transitionList->currentText())) ==
			    0) {
				newTransition = transitions.sources.array[i];
				break;
			}
		}
		obs_frontend_source_list_free(&transitions);
	}
	if (!newTransition) {
		auto item = scenesList->currentItem();
		if (item) {
			auto scene = obs_get_source_by_name(
				QT_TO_UTF8(item->text()));
			obs_set_output_source(outputChannel, scene);
			obs_source_release(scene);
		} else {
			obs_set_output_source(outputChannel, nullptr);
		}
		if (transition) {
			transition = nullptr;
			obs_transition_clear(oldTransition);
			obs_source_release(oldTransition);
		}
		return;
	}
	if (oldTransitionName &&
	    strcmp(oldTransitionName, obs_source_get_name(newTransition)) ==
		    0) {
		obs_source_release(newTransition);
		return;
	}
	obs_source_t *newTransitionCopy = obs_source_duplicate(
		newTransition, obs_source_get_name(newTransition), true);

	if (oldTransition) {
		obs_transition_swap_begin(newTransitionCopy, oldTransition);
		obs_set_output_source(outputChannel, newTransitionCopy);
		transition = newTransitionCopy;
		obs_transition_swap_end(newTransitionCopy, oldTransition);
		obs_transition_clear(oldTransition);
		obs_source_release(oldTransition);
	} else {
		auto item = scenesList->currentItem();
		if (item) {
			auto scene = obs_get_source_by_name(
				QT_TO_UTF8(item->text()));
			obs_transition_set(newTransitionCopy, scene);
			obs_source_release(scene);
		} else {
			obs_transition_set(newTransitionCopy, nullptr);
		}
		obs_set_output_source(outputChannel, newTransitionCopy);
		transition = newTransitionCopy;
	}
}

void DownstreamKeyer::Save(obs_data_t *data)
{
	obs_data_set_string(data, "name", QT_TO_UTF8(nameEdit->text()));
	obs_data_set_string(data, "transition",
			    QT_TO_UTF8(transitionList->currentText()));
	obs_data_set_int(data, "transition_duration",
			 transitionDuration->value());
	obs_data_array_t *sceneArray = obs_data_array_create();
	for (int i = 0; i < scenesList->count(); i++) {
		auto item = scenesList->item(i);
		if (!item)
			continue;
		auto sceneData = obs_data_create();
		obs_data_set_string(sceneData, "name",
				    QT_TO_UTF8(item->text()));
		obs_data_array_push_back(sceneArray, sceneData);
		obs_data_release(sceneData);
	}
	obs_data_set_array(data, "scenes", sceneArray);
	obs_data_set_string(
		data, "scene",
		scenesList->currentItem()
			? QT_TO_UTF8(scenesList->currentItem()->text())
			: "");
	obs_data_array_release(sceneArray);
}

void DownstreamKeyer::Load(obs_data_t *data)
{
	nameEdit->setText(QT_UTF8(obs_data_get_string(data, "name")));

	transitionList->clear();
	transitionList->addItem("");
	obs_frontend_source_list transitions = {0};
	obs_frontend_get_transitions(&transitions);
	for (size_t i = 0; i < transitions.sources.num; i++) {
		const char *n =
			obs_source_get_name(transitions.sources.array[i]);
		if (!n)
			continue;
		transitionList->addItem(QT_UTF8(n));
	}
	obs_frontend_source_list_free(&transitions);
	transitionList->setCurrentText(
		QT_UTF8(obs_data_get_string(data, "transition")));
	transitionDuration->setValue(
		obs_data_get_int(data, "transition_duration"));
	scenesList->clear();
	obs_data_array_t *sceneArray = obs_data_get_array(data, "scenes");
	auto sceneName = QT_UTF8(obs_data_get_string(data, "scene"));
	if (sceneArray) {
		auto count = obs_data_array_count(sceneArray);
		for (size_t i = 0; i < count; i++) {
			auto sceneData = obs_data_array_item(sceneArray, i);
			auto item = new QListWidgetItem(
				QT_UTF8(obs_data_get_string(sceneData, "name")));
			scenesList->addItem(item);
			if (item->text() == sceneName) {
				scenesList->setCurrentItem(item);
			}
			obs_data_release(sceneData);
		}
		obs_data_array_release(sceneArray);
	}
}

void DownstreamKeyer::UpdateTransitions()
{
	QString text = transitionList->currentText();
	transitionList->blockSignals(true);
	transitionList->clear();
	transitionList->addItem("");
	obs_frontend_source_list transitions = {0};
	obs_frontend_get_transitions(&transitions);
	for (size_t i = 0; i < transitions.sources.num; i++) {
		const char *n =
			obs_source_get_name(transitions.sources.array[i]);
		if (!n)
			continue;
		transitionList->addItem(QT_UTF8(n));
	}
	obs_frontend_source_list_free(&transitions);
	transitionList->setCurrentText(text);
	transitionList->blockSignals(false);
}

void DownstreamKeyer::source_rename(void *data, calldata_t *calldata)
{
	const auto downstreamKeyer = static_cast<DownstreamKeyer *>(data);
	const auto newName = QT_UTF8(calldata_string(calldata, "new_name"));
	const auto prevName = QT_UTF8(calldata_string(calldata, "prev_name"));
	const auto count = downstreamKeyer->scenesList->count();
	for (int i = 0; i < count; i++) {
		const auto item = downstreamKeyer->scenesList->item(i);
		if (item->text() == prevName)
			item->setText(newName);
	}
}

void DownstreamKeyer::source_remove(void *data, calldata_t *calldata)
{
	const auto downstreamKeyer = static_cast<DownstreamKeyer *>(data);
	const auto name = QT_UTF8(obs_source_get_name(
		static_cast<obs_source_t *>(calldata_ptr(calldata, "source"))));

	const auto count = downstreamKeyer->scenesList->count();
	for (int i = count - 1; i >= 0; i--) {
		const auto item = downstreamKeyer->scenesList->item(i);
		if (item->text() == name) {
			downstreamKeyer->scenesList->removeItemWidget(item);
			delete item;
		}
	}
}
