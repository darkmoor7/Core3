/*
				Copyright <SWGEmu>
		See file COPYING for copying conditions. */

#include "server/zone/objects/tangible/Container.h"
#include "server/zone/packets/object/ObjectMenuResponse.h"
#include "server/zone/objects/creature/CreatureObject.h"
#include "server/zone/objects/building/BuildingObject.h"
#include "server/zone/objects/installation/factory/FactoryObject.h"
#include "server/zone/objects/player/sui/inputbox/SuiInputBox.h"
#include "server/zone/objects/player/sessions/SlicingSession.h"
#include "server/zone/objects/tangible/tool/CraftingStation.h"
#include "server/zone/objects/tangible/wearables/WearableContainerObject.h"
#include "templates/tangible/ContainerTemplate.h"
#include "server/zone/objects/creature/ai/AiAgent.h"
#include "server/zone/objects/player/PlayerObject.h"

void ContainerImplementation::initializeTransientMembers() {
	TangibleObjectImplementation::initializeTransientMembers();
	relocking = false;
	setLoggingName("Container");
}

void ContainerImplementation::notifyLoadFromDatabase() {
	TangibleObjectImplementation::notifyLoadFromDatabase();
	if (getGameObjectType() == SceneObjectType::STATICLOOTCONTAINER) {
		if (System::random(100) < getLockChance()) {
			setLockedStatus(true);
			setSliceable(true);
			setSliced(false);
		} else {
			setLockedStatus(false);
			setSliceable(false);
			setSliced(false);
		}
	}
}
void ContainerImplementation::loadTemplateData(SharedObjectTemplate* templateData) {
	TangibleObjectImplementation::loadTemplateData(templateData);

	containerObjects.setDelayedLoadOperationMode();

	ContainerTemplate* containerTemplate = dynamic_cast<ContainerTemplate*>(templateData);

	if (containerTemplate == nullptr)
		return;

	locked = containerTemplate->getLocked();
}

void ContainerImplementation::fillObjectMenuResponse(ObjectMenuResponse* menuResponse, CreatureObject* player) {
	TangibleObjectImplementation::fillObjectMenuResponse(menuResponse, player);

	if (checkContainerPermission(player, ContainerPermissions::MOVECONTAINER && getParent().get() != nullptr && getParent().get()->checkContainerPermission(player, ContainerPermissions::MOVEOUT) && !(_this.getReferenceUnsafeStaticCast()->isRecycleToolObject()) && !(_this.getReferenceUnsafeStaticCast()->isAntiDecayKitObject())))

		menuResponse->addRadialMenuItem(50, 3, "@base_player:set_name"); // Set Name

	if (isSliceable() && isContainerLocked() && player->hasSkill("combat_smuggler_novice"))
		menuResponse->addRadialMenuItem(69, 3, "@slicing/slicing:slice"); // Slice
}

int ContainerImplementation::handleObjectMenuSelect(CreatureObject* player, byte selectedID) {
	if (selectedID == 50) {
		if (checkContainerPermission(player, ContainerPermissions::MOVECONTAINER && getParent().get() != nullptr && getParent().get()->checkContainerPermission(player, ContainerPermissions::MOVEOUT))) {
			ManagedReference<SuiInputBox*> inputBox = new SuiInputBox(player, SuiWindowType::OBJECT_NAME, 0x00);

			inputBox->setPromptTitle("@sui:set_name_title");
			inputBox->setPromptText("@sui:set_name_prompt");
			inputBox->setUsingObject(_this.getReferenceUnsafeStaticCast());
			inputBox->setMaxInputSize(255);

			inputBox->setDefaultInput(getCustomObjectName().toString());

			player->getPlayerObject()->addSuiBox(inputBox);
			player->sendMessage(inputBox->generateMessage());

			return 0;
		}

	} else if (selectedID == 69 && player->hasSkill("combat_smuggler_novice")) {
		ManagedReference<Facade*> facade = player->getActiveSession(SessionFacadeType::SLICING);
		ManagedReference<SlicingSession*> session = dynamic_cast<SlicingSession*>(facade.get());

		if (session != nullptr) {
			player->sendSystemMessage("@slicing/slicing:already_slicing");
			return 0;
		}

		// Create Session
		session = new SlicingSession(player);
		session->initalizeSlicingMenu(player, _this.getReferenceUnsafeStaticCast());

		return 0;
	}

	return TangibleObjectImplementation::handleObjectMenuSelect(player, selectedID);
}

int ContainerImplementation::canAddObject(SceneObject* object, int containmentType, String& errorDescription) {
	//	if (locked)
	//	return TransferErrorCode::CONTAINERLOCKED;

	if ((object->isIntangibleObject() && getContainerType() != 3) || (getContainerType() == 3 && !object->isIntangibleObject())) {
		errorDescription = "@container_error_message:container07"; // You cannot put that kind of item in that kind of container.

		return TransferErrorCode::INVALIDTYPE;
	}

	if (containmentType == -1) {
		if ((gameObjectType == SceneObjectType::WEARABLECONTAINER && object->getGameObjectType() == SceneObjectType::WEARABLECONTAINER)) {
			errorDescription = "@container_error_message:container12"; // This item is too bulky to fit inside this container.

			return TransferErrorCode::CANTNESTOBJECT;
		}

		if (object->isContainerObject() && getArrangementDescriptorSize() == 0) {
			errorDescription = "@container_error_message:container12"; // This item is too bulky to fit inside this container.

			return TransferErrorCode::CANTNESTOBJECT;
		}

		if (object->isCraftingStation()) {
			ManagedReference<SceneObject*> hopper = object->getSlottedObject("ingredient_hopper");

			if (hopper != nullptr && hopper->getCountableObjectsRecursive() > 0) {
				errorDescription = "@container_error_message:container21"; // You cannot pick up a crafting station unless it is empty.

				return TransferErrorCode::CANTNESTOBJECT;
			}
		}

		// A container placed in a player inventory or bank takes one slot there.
		// An unequipped backpack still occupies one slot per item inside it.
		int objectSize = 1;

		if (object->isWearableContainerObject())
			objectSize = object->getCountableObjectsRecursive() + 1;
		else if (object->isContainerObject() && !isPlayerInventoryOrBank())
			objectSize = object->getContainerObjectsSize() + 1;

		// Return if there's not enough room in the container
		if (getContainerVolumeLimit() < getCountableObjectsRecursive() + objectSize) {
			errorDescription = "@container_error_message:container03"; // This container is full.

			return TransferErrorCode::CONTAINERFULL;
		}

		ManagedReference<SceneObject*> wearableParent = getParentRecursively(SceneObjectType::WEARABLECONTAINER);
		ManagedReference<SceneObject*> playerParent = getParentRecursively(SceneObjectType::PLAYERCREATURE);

		// If there's a wearable container parent, return if it doesn't have enough room
		if (wearableParent != nullptr) {
			if (wearableParent->getContainerVolumeLimit() < wearableParent->getCountableObjectsRecursive() + objectSize) {
				errorDescription = "@container_error_message:container03"; // This container is full.

				return TransferErrorCode::CONTAINERFULL;
			}

			// An unequipped pack keeps using inventory or bank space as items are added.
			ManagedReference<WearableContainerObject*> wearable = cast<WearableContainerObject*>(wearableParent.get());

			if (wearable != nullptr && !wearable->isEquipped() && playerParent != nullptr) {
				SceneObject* inventory = playerParent->getSlottedObject("inventory");
				SceneObject* bank = playerParent->getSlottedObject("bank");
				SceneObject* parentOfWearableParent = wearable->getParent().get();

				if (inventory != nullptr && parentOfWearableParent == inventory) {
					if (inventory->getContainerVolumeLimit() < inventory->getCountableObjectsRecursive() + objectSize) {
						errorDescription = "@error_message:inv_full"; // Your inventory is full.

						return TransferErrorCode::CONTAINERFULL;
					}
				} else if (bank != nullptr && parentOfWearableParent == bank) {
					if (bank->getContainerVolumeLimit() < bank->getCountableObjectsRecursive() + objectSize) {
						errorDescription = "@container_error_message:container03"; // This container is full.

						return TransferErrorCode::CONTAINERFULL;
					}

					if (!playerParent->isNearBank())
						return TransferErrorCode::NOTNEARBANK;
				}
			}
		} else if (gameObjectType == SceneObjectType::WEARABLECONTAINER) {
			WearableContainerObject* pack = cast<WearableContainerObject*>(_this.getReferenceUnsafeStaticCast());

			if (pack != nullptr && !pack->isEquipped() && playerParent != nullptr) {
				SceneObject* inventory = playerParent->getSlottedObject("inventory");
				SceneObject* bank = playerParent->getSlottedObject("bank");
				SceneObject* thisParent = getParent().get();

				if (inventory != nullptr && thisParent == inventory) {
					if (inventory->getContainerVolumeLimit() < inventory->getCountableObjectsRecursive() + objectSize) {
						errorDescription = "@error_message:inv_full"; // Your inventory is full.

						return TransferErrorCode::CONTAINERFULL;
					}
				} else if (bank != nullptr && thisParent == bank) {
					if (bank->getContainerVolumeLimit() < bank->getCountableObjectsRecursive() + objectSize) {
						errorDescription = "@container_error_message:container03"; // This container is full.

						return TransferErrorCode::CONTAINERFULL;
					}

					if (!playerParent->isNearBank())
						return TransferErrorCode::NOTNEARBANK;
				}
			}
		}

		ManagedReference<SceneObject*> myParent = getParent().get();

		// Check if the container is in a building or factory ingredient hopper
		// If it is a droid object, don't consider these cases as it can still
		// receive items in a full house.
		bool isDroid = (myParent != nullptr && myParent->isDroidObject());

		if (playerParent == nullptr && !isDroid) {
			ManagedReference<SceneObject*> rootParent = getRootParent();

			if (rootParent != nullptr) {
				if (rootParent->isBuildingObject()) {
					BuildingObject* building = rootParent.castTo<BuildingObject*>();

					if (!building->isStaticBuilding() && (building->getCurrentNumberOfPlayerItems() + objectSize > building->getMaximumNumberOfPlayerItems())) {
						errorDescription = "@container_error_message:container13"; // This house has too many items in it

						return TransferErrorCode::TOOMANYITEMSINHOUSE;
					}
				} else if (rootParent->isFactory()) {
					FactoryObject* factory = rootParent.castTo<FactoryObject*>();
					Reference<SceneObject*> hopper = factory->getSlottedObject("ingredient_hopper");

					if (hopper == nullptr || hopper->getContainerVolumeLimit() < hopper->getCountableObjectsRecursive() + objectSize) {
						errorDescription = "@container_error_message:container03"; // This container is full.
						return TransferErrorCode::CONTAINERFULL;
					}
				} else if (rootParent->isPobShip() && ((rootParent->getCurrentNumberOfPlayerItems() + objectSize) > rootParent->getMaximumNumberOfPlayerItems())) {
					errorDescription = "@container_error_message:container13"; // This house has too many items in it

					return TransferErrorCode::TOOMANYITEMSINHOUSE;
				}
			}
		}

		ManagedReference<SceneObject*> otherParent = object->getParent().get();

		if (myParent != nullptr && otherParent != nullptr) {
			if (otherParent->isCreatureObject()) {
				AiAgent* ai = dynamic_cast<AiAgent*>(otherParent.get());

				if (ai != nullptr) {
					SceneObject* creatureInventory = ai->getSlottedObject("inventory");

					if (creatureInventory != nullptr) {
						uint64 lootOwnerID = creatureInventory->getContainerPermissions()->getOwnerID();

						if (lootOwnerID != myParent->getObjectID()) {
							errorDescription = "@group:no_loot_permission";

							return TransferErrorCode::NOLOOTPERMISSION;
						}
					}
				}
			}
		}
	}

	return TangibleObjectImplementation::canAddObject(object, containmentType, errorDescription);
}
