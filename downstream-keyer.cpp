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
#include <obs-frontend-api.h>

#include "obs-module.h"

#define QT_UTF8(str) QString::fromUtf8(str)
#define QT_TO_UTF8(str) str.toUtf8().constData()

extern obs_websocket_vendor vendor;

DownstreamKeyer::DownstreamKeyer(int channel, QString name, obs_view_t *v,
				 get_transitions_callback_t gt, void *gtd)
	: outputChannel(channel),
	  transition(nullptr),
	  showTransition(nullptr),
	  hideTransition(nullptr),
	  overrideTransition(nullptr),
	  transitionDuration(300),
	  showTransitionDuration(300),
	  hideTransitionDuration(300),
	  view(v),
	  get_transitions(gt),
	  get_transitions_data(gtd)
{
	setObjectName(name);
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
	QString disableDskHotkeyName = QT_UTF8(obs_module_text("DisableDSK"));
	disableDskHotkeyName += " ";
	disableDskHotkeyName += name;

	null_hotkey_id = obs_hotkey_register_frontend(
		QT_TO_UTF8(disableDskHotkeyName),
		QT_TO_UTF8(disableDskHotkeyName), null_hotkey, this);
	QString enableTieHotkeyName = QT_UTF8(obs_module_text("EnableTie"));
	enableTieHotkeyName += " ";
	enableTieHotkeyName += name;
	QString disableTieHotkeyName = QT_UTF8(obs_module_text("DisableTie"));
	disableTieHotkeyName += " ";
	disableTieHotkeyName += name;

	tie_hotkey_id = obs_hotkey_pair_register_frontend(
		QT_TO_UTF8(enableTieHotkeyName),
		QT_TO_UTF8(enableTieHotkeyName),
		QT_TO_UTF8(disableTieHotkeyName),
		QT_TO_UTF8(disableTieHotkeyName), enable_tie_hotkey,
		disable_tie_hotkey, this, this);
}

DownstreamKeyer::~DownstreamKeyer()
{
	if (view) {
		//obs_view_set_source(view, outputChannel, nullptr);
	} else {
		obs_set_output_source(outputChannel, nullptr);
	}
	obs_hotkey_unregister(null_hotkey_id);
	obs_hotkey_pair_unregister(tie_hotkey_id);

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
	if (overrideTransition) {
		obs_transition_clear(overrideTransition);
		obs_source_release(overrideTransition);
		overrideTransition = nullptr;
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
	obs_source_t *scene = nullptr;
	if (view) {
		obs_source_t *source = obs_view_get_source(view, 0);
		if (source &&
		    obs_source_get_type(source) == OBS_SOURCE_TYPE_TRANSITION) {
			obs_source_t *ts =
				obs_transition_get_active_source(source);
			if (ts) {
				obs_source_release(source);
				source = ts;
			}
		}
		if (source && obs_source_is_scene(source)) {
			scene = source;
		} else {
			obs_source_release(source);
		}
	} else {
		scene = obs_frontend_preview_program_mode_active()
				? obs_frontend_get_current_preview_scene()
				: obs_frontend_get_current_scene();
	}
	if (!scene)
		return;
	auto sceneName = QT_UTF8(obs_source_get_name(scene));
	if (scenesList->findItems(sceneName, Qt::MatchFixedString).count() ==
	    0) {
		add_scene(sceneName, scene);
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

void DownstreamKeyer::apply_source(obs_source_t *const newSource)
{
	obs_source_t *prevSource =
		view ? obs_view_get_source(view, outputChannel)
		     : obs_get_output_source(outputChannel);
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
	} else {
		auto ph = obs_get_proc_handler();
		calldata_t cd = {0};
		calldata_set_string(&cd, "from_scene",
				    obs_source_get_name(prevSource));
		calldata_set_string(&cd, "to_scene",
				    obs_source_get_name(newSource));
		if (proc_handler_call(ph, "get_transition_table_transition",
				      &cd)) {
			const char *p = calldata_string(&cd, "transition");
			SetTransition(p ? p : "", transitionType::override);
			SetTransitionDuration(calldata_int(&cd, "duration"),
					      transitionType::override);
		} else {
			SetTransition("", transitionType::override);
		}
		calldata_free(&cd);
		if (overrideTransition) {
			newTransition = overrideTransition;
			newTransitionDuration = overrideTransitionDuration;
		} else if (transition)
			newTransition = transition;
	}
	if (prevSource == newSource) {
		//skip if nothing changed
	} else {
		if (!newTransition) {
			if (view) {
				obs_view_set_source(view, outputChannel,
						    newSource);
			} else {
				obs_set_output_source(outputChannel, newSource);
			}
		} else {
			obs_transition_set(newTransition, prevSource);

			obs_transition_start(newTransition,
					     OBS_TRANSITION_MODE_AUTO,
					     newTransitionDuration, newSource);

			if (prevTransition != newTransition) {
				if (view) {
					obs_view_set_source(view, outputChannel,
							    newTransition);
				} else {
					obs_set_output_source(outputChannel,
							      newTransition);
				}
			}
		}
		if (vendor) {
			const auto data = obs_data_create();
			obs_data_set_string(data, "dsk_name",
					    QT_TO_UTF8(objectName()));
			obs_data_set_int(data, "dsk_channel", outputChannel);
			obs_data_set_string(
				data, "new_scene",
				newSource ? obs_source_get_name(newSource)
					  : "");
			obs_data_set_string(
				data, "old_scene",
				prevSource ? obs_source_get_name(prevSource)
					   : "");
			obs_websocket_vendor_emit_event(
				vendor, "dsk_scene_changed", data);
			obs_data_release(data);
		}
	}

	obs_source_release(prevSource);
	obs_source_release(prevTransition);
}

void DownstreamKeyer::apply_selected_source()
{
	const auto l = scenesList->selectedItems();
	const auto newSource =
		l.count()
			? obs_get_source_by_name(QT_TO_UTF8(l.value(0)->text()))
			: nullptr;

	apply_source(newSource);
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

	obs_data_array_t *nh = obs_hotkey_save(null_hotkey_id);
	obs_data_set_array(data, "null_hotkey", nh);
	obs_data_array_release(nh);
	obs_data_array_t *eth = nullptr;
	obs_data_array_t *dth = nullptr;
	obs_hotkey_pair_save(tie_hotkey_id, &eth, &dth);
	obs_data_set_array(data, "enable_tie_hotkey", eth);
	obs_data_set_array(data, "disable_tie_hotkey", dth);
	obs_data_array_release(eth);
	obs_data_array_release(dth);
	auto excludes = obs_data_array_create();
	for (auto t : exclude_scenes) {
		const auto obj = obs_data_create();
		obs_data_set_string(obj, "name", t.c_str());
		obs_data_array_push_back(excludes, obj);
		obs_data_release(obj);
	}
	obs_data_set_array(data, "exclude_scenes", excludes);
	obs_data_array_release(excludes);
}

std::string DownstreamKeyer::GetTransition(enum transitionType transition_type)
{
	if (transition_type == transitionType::match && transition)
		return obs_source_get_name(transition);
	if (transition_type == transitionType::show && showTransition)
		return obs_source_get_name(showTransition);
	if (transition_type == transitionType::hide && hideTransition)
		return obs_source_get_name(hideTransition);
	if (transition_type == transitionType::override && overrideTransition)
		return obs_source_get_name(overrideTransition);
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
	else if (transition_type == transitionType::override)
		oldTransition = overrideTransition;

	if (!oldTransition && (!transition_name || !strlen(transition_name)))
		return;

	obs_source_t *newTransition = nullptr;
	obs_frontend_source_list transitions = {};
	get_transitions(get_transitions_data, &transitions);
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
	else if (transition_type == transitionType::override)
		overrideTransition = newTransition;
	else
		transition = newTransition;
	obs_source_t *prevSource =
		view ? obs_view_get_source(view, outputChannel)
		     : obs_get_output_source(outputChannel);
	if (oldTransition && prevSource == oldTransition) {
		if (newTransition) {
			//swap transition
			obs_transition_swap_begin(newTransition, oldTransition);
			if (view) {
				obs_view_set_source(view, outputChannel,
						    newTransition);
			} else {
				obs_set_output_source(outputChannel,
						      newTransition);
			}
			obs_transition_swap_end(newTransition, oldTransition);
		} else {
			auto item = scenesList->currentItem();
			if (item) {
				auto scene = obs_get_source_by_name(
					QT_TO_UTF8(item->text()));
				if (view) {
					obs_view_set_source(view, outputChannel,
							    scene);
				} else {
					obs_set_output_source(outputChannel,
							      scene);
				}
				obs_source_release(scene);
			} else {
				if (view) {
					obs_view_set_source(view, outputChannel,
							    nullptr);
				} else {
					obs_set_output_source(outputChannel,
							      nullptr);
				}
			}
		}
	}
	obs_source_release(prevSource);
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
	else if (transition_type == transitionType::override)
		overrideTransitionDuration = duration;
}

int DownstreamKeyer::GetTransitionDuration(enum transitionType transition_type)
{
	if (transition_type == transitionType::show)
		return showTransitionDuration;
	if (transition_type == transitionType::hide)
		return hideTransitionDuration;
	if (transition_type == transitionType::override)
		return overrideTransitionDuration;
	return transitionDuration;
}

void DownstreamKeyer::SceneChanged(std::string scene)
{
	auto found = false;
	for (const auto &e : exclude_scenes) {
		if (scene == e)
			found = true;
	}

	if (found) {
		apply_source(nullptr);
		return;
	} else {
		obs_source_t *prevSource =
			view ? obs_view_get_source(view, outputChannel)
			     : obs_get_output_source(outputChannel);
		if (prevSource && obs_source_get_type(prevSource) ==
					  OBS_SOURCE_TYPE_TRANSITION) {
			obs_source_t *prevTransition = prevSource;
			prevSource = obs_transition_get_active_source(
				prevTransition);
			obs_source_release(prevTransition);
		}
		if (prevSource == nullptr) {
			apply_selected_source();
			return;
		}
		obs_source_release(prevSource);
	}

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
				if (source) {
					if (view) {
						obs_view_set_source(
							view, outputChannel,
							source);
					} else {
						obs_set_output_source(
							outputChannel, source);
					}
				}
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
	if (sceneName.isEmpty()) {
		for (int i = 0; i < scenesList->count(); i++) {
			scenesList->item(i)->setSelected(false);
		}
		const auto row = scenesList->currentRow();
		if (row != -1)
			scenesList->setCurrentRow(-1);
	}
	obs_data_array_t *nh = obs_data_get_array(data, "null_hotkey");
	obs_hotkey_load(null_hotkey_id, nh);
	obs_data_array_release(nh);
	obs_data_array_t *eth = obs_data_get_array(data, "enable_tie_hotkey");
	obs_data_array_t *dth = obs_data_get_array(data, "disable_tie_hotkey");
	obs_hotkey_pair_load(tie_hotkey_id, eth, dth);
	obs_data_array_release(eth);
	obs_data_array_release(dth);

	auto excludes = obs_data_get_array(data, "exclude_scenes");
	exclude_scenes.clear();
	if (excludes) {
		auto count = obs_data_array_count(excludes);
		for (size_t i = 0; i < count; i++) {
			const auto sceneData = obs_data_array_item(excludes, i);
			exclude_scenes.emplace(
				obs_data_get_string(sceneData, "name"));
			obs_data_release(sceneData);
		}
		obs_data_array_release(excludes);
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
	UNUSED_PARAMETER(hotkey);
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
	UNUSED_PARAMETER(hotkey);
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

void DownstreamKeyer::null_hotkey(void *data, obs_hotkey_id id,
				  obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
	if (!pressed)
		return;
	const auto downstreamKeyer = static_cast<DownstreamKeyer *>(data);
	QMetaObject::invokeMethod(downstreamKeyer,
				  "on_actionSceneNull_triggered",
				  Qt::QueuedConnection);
}

bool DownstreamKeyer::enable_tie_hotkey(void *data, obs_hotkey_pair_id id,
					obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
	if (!pressed)
		return false;
	const auto downstreamKeyer = static_cast<DownstreamKeyer *>(data);
	if (downstreamKeyer->tie->isChecked())
		return false;
	downstreamKeyer->tie->setChecked(true);
	return true;
}
bool DownstreamKeyer::disable_tie_hotkey(void *data, obs_hotkey_pair_id id,
					 obs_hotkey_t *hotkey, bool pressed)
{
	UNUSED_PARAMETER(id);
	UNUSED_PARAMETER(hotkey);
	if (!pressed)
		return false;
	const auto downstreamKeyer = static_cast<DownstreamKeyer *>(data);
	if (!downstreamKeyer->tie->isChecked())
		return false;
	downstreamKeyer->tie->setChecked(false);
	return true;
}

void DownstreamKeyer::AddExcludeScene(const char *scene_name)
{
	if (exclude_scenes.count(scene_name) > 0)
		return;
	exclude_scenes.emplace(scene_name);
	obs_source_t *scene = nullptr;
	if (view) {
		obs_source_t *source = obs_view_get_source(view, 0);
		if (source &&
		    obs_source_get_type(source) == OBS_SOURCE_TYPE_TRANSITION) {
			obs_source_t *ts =
				obs_transition_get_active_source(source);
			if (ts) {
				obs_source_release(source);
				source = ts;
			}
		}
		if (source && obs_source_is_scene(source)) {
			scene = source;
		} else {
			obs_source_release(source);
		}
	} else {
		scene = obs_frontend_get_current_scene();
	}
	const auto sn = obs_source_get_name(scene);
	if (strcmp(sn, scene_name) == 0)
		SceneChanged(sn);
	obs_source_release(scene);
}

void DownstreamKeyer::RemoveExcludeScene(const char *scene_name)
{
	exclude_scenes.erase(scene_name);
	obs_source_t *scene = nullptr;
	if (view) {
		obs_source_t *source = obs_view_get_source(view, 0);
		if (source &&
		    obs_source_get_type(source) == OBS_SOURCE_TYPE_TRANSITION) {
			obs_source_t *ts =
				obs_transition_get_active_source(source);
			if (ts) {
				obs_source_release(source);
				source = ts;
			}
		}
		if (source && obs_source_is_scene(source)) {
			scene = source;
		} else {
			obs_source_release(source);
		}
	} else {
		scene = obs_frontend_get_current_scene();
	}
	const auto sn = obs_source_get_name(scene);
	if (strcmp(sn, scene_name) == 0)
		SceneChanged(sn);
	obs_source_release(scene);
}
bool DownstreamKeyer::IsSceneExcluded(const char *scene_name)
{
	return exclude_scenes.find(scene_name) != exclude_scenes.end();
}

bool DownstreamKeyer::SwitchToScene(QString scene_name)
{
	if (scene_name.isEmpty()) {
		on_actionSceneNull_triggered();
		return true;
	}
	for (int i = 0; i < scenesList->count(); i++) {
		const auto item = scenesList->item(i);
		if (!item)
			continue;
		if (item->text() == scene_name) {
			if (!item->isSelected()) {
				item->setSelected(true);
			}
			return true;
		}
	}
	return false;
}

void DownstreamKeyer::add_scene(QString scene_name, obs_source_t *s)
{
	const auto item = new QListWidgetItem(scene_name);
	scenesList->addItem(item);

	std::string enable_hotkey = obs_module_text("EnableDSK");
	enable_hotkey += " ";
	enable_hotkey += QT_TO_UTF8(objectName());
	std::string disable_hotkey = obs_module_text("DisableDSK");
	disable_hotkey += " ";
	disable_hotkey += QT_TO_UTF8(objectName());
	uint64_t h = obs_hotkey_pair_register_source(
		s, enable_hotkey.c_str(), enable_hotkey.c_str(),
		disable_hotkey.c_str(), disable_hotkey.c_str(),
		enable_DSK_hotkey, disable_DSK_hotkey, this, this);

	if (h != OBS_INVALID_HOTKEY_PAIR_ID) {
		item->setData(Qt::UserRole, static_cast<uint>(h));
	}
}

bool DownstreamKeyer::AddScene(QString scene_name)
{
	if (scene_name.isEmpty()) {
		return false;
	}
	if (scenesList->findItems(scene_name, Qt::MatchFixedString).count() !=
	    0) {
		return true;
	}
	auto nameUtf8 = scene_name.toUtf8();
	auto name = nameUtf8.constData();
	auto s = obs_get_source_by_name(name);
	if (obs_source_is_scene(s)) {

		add_scene(scene_name, s);
		obs_source_release(s);
		return true;
	}
	obs_source_release(s);
	return false;
}

bool DownstreamKeyer::RemoveScene(QString scene_name)
{
	if (scene_name.isEmpty()) {
		return false;
	}
	for (int i = 0; i < scenesList->count(); i++) {
		const auto item = scenesList->item(i);
		if (!item)
			continue;

		if (item->text() == scene_name) {
			scenesList->removeItemWidget(item);
			obs_hotkey_pair_unregister(
				item->data(Qt::UserRole).toUInt());
			delete item;
			return true;
		}
	}
	return false;
}

void DownstreamKeyer::SetTie(bool tie)
{
	this->tie->setChecked(tie);
}

void DownstreamKeyer::SetOutputChannel(int oc)
{
	if (oc == outputChannel)
		return;
	obs_source_t *prevSource =
		view ? obs_view_get_source(view, outputChannel)
		     : obs_get_output_source(outputChannel);
	obs_source_t *prevTransition = nullptr;
	if (prevSource &&
	    obs_source_get_type(prevSource) == OBS_SOURCE_TYPE_TRANSITION) {
		prevTransition = prevSource;
		prevSource = obs_transition_get_active_source(prevSource);
	}
	if (prevTransition) {
		if (prevTransition == transition ||
		    prevTransition == showTransition ||
		    prevTransition == hideTransition ||
		    prevTransition == overrideTransition) {
			if (view) {
				obs_view_set_source(view, outputChannel,
						    nullptr);
			} else {
				obs_set_output_source(outputChannel, nullptr);
			}
		} else {
			obs_source_release(prevTransition);
			prevTransition = nullptr;
		}
	} else if (prevSource) {
		const auto l = scenesList->selectedItems();
		const auto newSource =
			l.count() ? obs_get_source_by_name(
					    QT_TO_UTF8(l.value(0)->text()))
				  : nullptr;
		if (prevSource == newSource) {
			if (view) {
				obs_view_set_source(view, outputChannel,
						    nullptr);
			} else {
				obs_set_output_source(outputChannel, nullptr);
			}
			obs_source_release(newSource);
		} else {
			obs_source_release(prevSource);
			prevSource = newSource;
		}
	}
	outputChannel = oc;
	if (prevTransition) {
		if (view) {
			obs_view_set_source(view, outputChannel,
					    prevTransition);
		} else {
			obs_set_output_source(outputChannel, prevTransition);
		}
	} else {
		apply_selected_source();
	}
	obs_source_release(prevSource);
	obs_source_release(prevTransition);
}

LockedCheckBox::LockedCheckBox()
{
	setProperty("lockCheckBox", true);
}

LockedCheckBox::LockedCheckBox(QWidget *parent) : QCheckBox(parent) {}
