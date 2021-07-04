#include "downstream-keyer.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QToolBar>
#include <QVBoxLayout>
#include <../UI/obs-frontend-api/obs-frontend-api.h>

#include "obs-module.h"

#define QT_UTF8(str) QString::fromUtf8(str)
#define QT_TO_UTF8(str) str.toUtf8().constData()

DownstreamKeyer::DownstreamKeyer(int channel)
	: outputChannel(channel),
	  transition(nullptr),
	  showTransition(nullptr),
	  hideTransition(nullptr),
	  transitionDuration(300),
	  showTransitionDuration(300),
	  hideTransitionDuration(300)
{
	auto layout = new QVBoxLayout(this);
	layout->setSpacing(0);
	layout->setContentsMargins(0, 0, 0, 0);

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
	connect(scenesList, SIGNAL(itemSelectionChanged()), this,
		SLOT(on_scenesList_itemSelectionChanged()));

	layout->addWidget(scenesList);

	scenesToolbar = new QToolBar(this);
	scenesToolbar->setObjectName(QStringLiteral("scenesToolbar"));
	scenesToolbar->setIconSize(QSize(16, 16));
	scenesToolbar->setFloatable(false);

	auto actionAddScene = new QAction(this);
	actionAddScene->setObjectName(QStringLiteral("actionAddScene"));
	actionAddScene->setProperty("themeID", "addIconSmall");
	actionAddScene->setText(QT_UTF8(obs_module_text("Add")));
	connect(actionAddScene, SIGNAL(triggered()), this,
		SLOT(on_actionAddScene_triggered()));
	scenesToolbar->addAction(actionAddScene);

	auto actionRemoveScene = new QAction(this);
	actionRemoveScene->setObjectName(QStringLiteral("actionRemoveScene"));
	actionRemoveScene->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	actionRemoveScene->setProperty("themeID", "removeIconSmall");
	actionRemoveScene->setText(QT_UTF8(obs_module_text("Remove")));
	connect(actionRemoveScene, SIGNAL(triggered()), this,
		SLOT(on_actionRemoveScene_triggered()));
	scenesToolbar->addAction(actionRemoveScene);

	scenesToolbar->addSeparator();

	auto actionSceneUp = new QAction(this);
	actionSceneUp->setObjectName(QStringLiteral("actionSceneUp"));
	actionSceneUp->setProperty("themeID", "upArrowIconSmall");
	actionSceneUp->setText(QT_UTF8(obs_module_text("MoveUp")));
	connect(actionSceneUp, SIGNAL(triggered()), this,
		SLOT(on_actionSceneUp_triggered()));
	scenesToolbar->addAction(actionSceneUp);

	auto actionSceneDown = new QAction(this);
	actionSceneDown->setObjectName(QStringLiteral("actionSceneDown"));
	actionSceneDown->setProperty("themeID", "downArrowIconSmall");
	actionSceneDown->setText(QT_UTF8(obs_module_text("MoveDown")));
	connect(actionSceneDown, SIGNAL(triggered()), this,
		SLOT(on_actionSceneDown_triggered()));
	scenesToolbar->addAction(actionSceneDown);

	scenesToolbar->addSeparator();

	auto actionSceneNull = new QAction(this);
	actionSceneNull->setObjectName(QStringLiteral("actionSceneNull"));
	actionSceneNull->setProperty("themeID", "pauseIconSmall");
	actionSceneNull->setText(QT_UTF8(obs_module_text("None")));
	connect(actionSceneNull, SIGNAL(triggered()), this,
		SLOT(on_actionSceneNull_triggered()));
	scenesToolbar->addAction(actionSceneNull);

	scenesToolbar->addSeparator();

	tie = new LockedCheckBox(this);
	tie->setObjectName(QStringLiteral("tie"));
	tie->setToolTip(QT_UTF8(obs_module_text("Tie")));
	scenesToolbar->addWidget(tie);

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
		transition = nullptr;
	}
	if (showTransition) {
		obs_transition_clear(showTransition);
		obs_source_release(showTransition);
		showTransition = nullptr;
	}
	if (hideTransition) {
		obs_transition_clear(hideTransition);
		obs_source_release(hideTransition);
		hideTransition = nullptr;
	}
	const auto sh = obs_get_signal_handler();
	signal_handler_disconnect(sh, "source_rename", source_rename, this);
	signal_handler_disconnect(sh, "source_remove", source_remove, this);
	while (scenesList->count()) {
		const auto item = scenesList->item(0);
		scenesList->removeItemWidget(item);
		obs_hotkey_pair_unregister(item->data(Qt::UserRole).toUInt());
		delete item;
	}
	delete scenesList;
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
		const auto item = new QListWidgetItem(sceneName);
		scenesList->addItem(item);

		std::string enable_hotkey = obs_module_text("EnableDSK");
		enable_hotkey += " ";
		enable_hotkey += QT_TO_UTF8(objectName());
		std::string disable_hotkey = obs_module_text("DisableDSK");
		disable_hotkey += " ";
		disable_hotkey += QT_TO_UTF8(objectName());
		uint64_t h = obs_hotkey_pair_register_source(
			scene, enable_hotkey.c_str(), enable_hotkey.c_str(),
			disable_hotkey.c_str(), disable_hotkey.c_str(),
			enable_DSK_hotkey, disable_DSK_hotkey, this, this);

		if (h != OBS_INVALID_HOTKEY_PAIR_ID) {
			item->setData(Qt::UserRole, static_cast<uint>(h));
		}
	}

	obs_source_release(scene);
}

void DownstreamKeyer::on_actionRemoveScene_triggered()
{
	auto item = scenesList->currentItem();
	if (!item)
		return;
	scenesList->removeItemWidget(item);
	obs_hotkey_pair_unregister(item->data(Qt::UserRole).toUInt());
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
	for (int i = 0; i < scenesList->count(); i++) {
		auto item = scenesList->item(i);
		item->setSelected(false);
	}
	scenesList->setCurrentRow(-1);
}

void DownstreamKeyer::apply_selected_source()
{
	const auto l = scenesList->selectedItems();
	const auto newSource =
		l.count()
			? obs_get_source_by_name(QT_TO_UTF8(l.value(0)->text()))
			: nullptr;

	obs_source_t *prevSource = obs_get_output_source(outputChannel);
	obs_source_t *prevTransition = nullptr;
	if (prevSource &&
	    obs_source_get_type(prevSource) == OBS_SOURCE_TYPE_TRANSITION) {
		prevTransition = prevSource;
		prevSource = obs_transition_get_active_source(prevSource);
	}
	obs_source_t *newTransition = nullptr;
	uint32_t newTransitionDuration = transitionDuration;
	if (prevSource == newSource) {
	} else if (!prevSource && newSource && showTransition) {
		newTransition = showTransition;
		newTransitionDuration = showTransitionDuration;
	} else if (prevSource && !newSource && hideTransition) {
		newTransition = hideTransition;
		newTransitionDuration = hideTransitionDuration;
	} else if (transition) {
		newTransition = transition;
	}
	if (prevSource == newSource) {
		//skip if nothing changed
	} else if (!newTransition) {
		obs_set_output_source(outputChannel, newSource);
	} else {
		obs_transition_set(newTransition, prevSource);

		obs_transition_start(newTransition, OBS_TRANSITION_MODE_AUTO,
				     newTransitionDuration, newSource);

		if (prevTransition != newTransition)
			obs_set_output_source(outputChannel, newTransition);
	}

	obs_source_release(prevSource);
	obs_source_release(prevTransition);
	obs_source_release(newSource);
}

void DownstreamKeyer::on_scenesList_itemSelectionChanged()
{
	if (tie->isChecked())
		return;

	apply_selected_source();
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

void DownstreamKeyer::Save(obs_data_t *data)
{
	obs_data_set_string(data, "transition",
			    transition ? obs_source_get_name(transition) : "");
	obs_data_set_int(data, "transition_duration", transitionDuration);
	obs_data_set_string(data, "show_transition",
			    showTransition ? obs_source_get_name(showTransition)
					   : "");
	obs_data_set_int(data, "show_transition_duration",
			 showTransitionDuration);
	obs_data_set_string(data, "hide_transition",
			    hideTransition ? obs_source_get_name(hideTransition)
					   : "");
	obs_data_set_int(data, "hide_transition_duration",
			 hideTransitionDuration);
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

std::string DownstreamKeyer::GetTransition(enum transitionType transition_type)
{
	if (transition_type == transitionType::match && transition)
		return obs_source_get_name(transition);
	if (transition_type == transitionType::show && showTransition)
		return obs_source_get_name(showTransition);
	if (transition_type == transitionType::hide && hideTransition)
		return obs_source_get_name(hideTransition);
	return "";
}

void DownstreamKeyer::SetTransition(const char *transition_name,
				    enum transitionType transition_type)
{
	obs_source_t *oldTransition = transition;
	if (transition_type == transitionType::show)
		oldTransition = showTransition;
	else if (transition_type == transitionType::hide)
		oldTransition = hideTransition;

	if (!oldTransition && (!transition_name || !strlen(transition_name)))
		return;

	obs_source_t *newTransition = nullptr;
	obs_frontend_source_list transitions = {0};
	obs_frontend_get_transitions(&transitions);
	for (size_t i = 0; i < transitions.sources.num; i++) {
		const char *n =
			obs_source_get_name(transitions.sources.array[i]);
		if (!n)
			continue;
		if (strcmp(transition_name, n) == 0) {
			newTransition = obs_source_duplicate(
				transitions.sources.array[i],
				obs_source_get_name(
					transitions.sources.array[i]),
				true);
			break;
		}
	}
	obs_frontend_source_list_free(&transitions);

	if (transition_type == transitionType::show)
		showTransition = newTransition;
	else if (transition_type == transitionType::hide)
		hideTransition = newTransition;
	else
		transition = newTransition;

	if (oldTransition &&
	    obs_get_output_source(outputChannel) == oldTransition) {
		if (newTransition) {
			//swap transition
			obs_transition_swap_begin(newTransition, oldTransition);
			obs_set_output_source(outputChannel, newTransition);
			obs_transition_swap_end(newTransition, oldTransition);
		} else {
			auto item = scenesList->currentItem();
			if (item) {
				auto scene = obs_get_source_by_name(
					QT_TO_UTF8(item->text()));
				obs_set_output_source(outputChannel, scene);
				obs_source_release(scene);
			} else {
				obs_set_output_source(outputChannel, nullptr);
			}
		}
	}
	if (oldTransition) {
		obs_transition_clear(oldTransition);
		obs_source_release(oldTransition);
	}
}

void DownstreamKeyer::SetTransitionDuration(int duration,
					    enum transitionType transition_type)
{
	if (transition_type == match)
		transitionDuration = duration;
	else if (transition_type == transitionType::show)
		showTransitionDuration = duration;
	else if (transition_type == transitionType::hide)
		hideTransitionDuration = duration;
}

int DownstreamKeyer::GetTransitionDuration(enum transitionType transition_type)
{
	if (transition_type == transitionType::show)
		return showTransitionDuration;
	if (transition_type == transitionType::hide)
		return hideTransitionDuration;
	return transitionDuration;
}

void DownstreamKeyer::SceneChanged()
{
	if (!tie->isChecked())
		return;

	apply_selected_source();
}

void DownstreamKeyer::Load(obs_data_t *data)
{
	SetTransition(obs_data_get_string(data, "transition"));
	transitionDuration = obs_data_get_int(data, "transition_duration");
	SetTransition(obs_data_get_string(data, "show_transition"),
		      transitionType::show);
	showTransitionDuration =
		obs_data_get_int(data, "show_transition_duration");
	SetTransition(obs_data_get_string(data, "hide_transition"),
		      transitionType::hide);
	hideTransitionDuration =
		obs_data_get_int(data, "hide_transition_duration");
	scenesList->clear();
	obs_data_array_t *sceneArray = obs_data_get_array(data, "scenes");
	const auto sceneName = QT_UTF8(obs_data_get_string(data, "scene"));
	if (sceneArray) {
		auto count = obs_data_array_count(sceneArray);
		for (size_t i = 0; i < count; i++) {
			const auto sceneData =
				obs_data_array_item(sceneArray, i);
			const auto source_name =
				obs_data_get_string(sceneData, "name");
			const auto item =
				new QListWidgetItem(QT_UTF8(source_name));
			scenesList->addItem(item);
			obs_source_t *source =
				obs_get_source_by_name(source_name);
			if (item->text() == sceneName) {
				if (source)
					obs_set_output_source(outputChannel,
							      source);

				scenesList->setCurrentItem(item);
				item->setSelected(true);
			}
			obs_data_release(sceneData);

			if (source) {
				std::string enable_hotkey =
					obs_module_text("EnableDSK");
				enable_hotkey += " ";
				enable_hotkey += QT_TO_UTF8(objectName());
				std::string disable_hotkey =
					obs_module_text("DisableDSK");
				disable_hotkey += " ";
				disable_hotkey += QT_TO_UTF8(objectName());
				uint64_t h = obs_hotkey_pair_register_source(
					source, enable_hotkey.c_str(),
					enable_hotkey.c_str(),
					disable_hotkey.c_str(),
					disable_hotkey.c_str(),
					enable_DSK_hotkey, disable_DSK_hotkey,
					this, this);

				if (h != OBS_INVALID_HOTKEY_PAIR_ID) {
					item->setData(Qt::UserRole,
						      static_cast<uint>(h));
				}
				obs_source_release(source);
			}
		}
		obs_data_array_release(sceneArray);
	}
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
			obs_hotkey_pair_unregister(
				item->data(Qt::UserRole).toUInt());
			delete item;
		}
	}
}

bool DownstreamKeyer::enable_DSK_hotkey(void *data, obs_hotkey_pair_id id,
					obs_hotkey_t *hotkey, bool pressed)
{
	if (!pressed)
		return false;
	const auto downstreamKeyer = static_cast<DownstreamKeyer *>(data);
	bool changed = false;
	for (int i = 0; i < downstreamKeyer->scenesList->count(); i++) {
		auto item = downstreamKeyer->scenesList->item(i);
		if (!item)
			continue;
		if (item->data(Qt::UserRole).toUInt() == id) {
			if (!item->isSelected()) {
				item->setSelected(true);
				changed = true;
			}
		}
	}
	return changed;
}

bool DownstreamKeyer::disable_DSK_hotkey(void *data, obs_hotkey_pair_id id,
					 obs_hotkey_t *hotkey, bool pressed)
{
	if (!pressed)
		return false;
	const auto downstreamKeyer = static_cast<DownstreamKeyer *>(data);
	bool changed = false;
	for (int i = 0; i < downstreamKeyer->scenesList->count(); i++) {
		auto item = downstreamKeyer->scenesList->item(i);
		if (!item)
			continue;
		if (item->data(Qt::UserRole).toUInt() == id) {
			if (item->isSelected()) {
				item->setSelected(false);
				changed = true;
			}
		}
	}
	return changed;
}

LockedCheckBox::LockedCheckBox() {}

LockedCheckBox::LockedCheckBox(QWidget *parent) : QCheckBox(parent) {}
