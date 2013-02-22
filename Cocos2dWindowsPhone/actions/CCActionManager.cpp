/*
* cocos2d-x   http://www.cocos2d-x.org
*
* Copyright (c) 2010-2011 - cocos2d-x community
* Copyright (c) 2010-2011 cocos2d-x.org
* Copyright (c) 2008-2010 Ricardo Quesada
* Copyright (c) 2009      Valentin Milea
* Copyright (c) 2011      Zynga Inc.
* 
* Portions Copyright (c) Microsoft Open Technologies, Inc.
* All Rights Reserved
* 
* Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. 
* You may obtain a copy of the License at 
* 
* http://www.apache.org/licenses/LICENSE-2.0 
* 
* Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an 
* "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. 
* See the License for the specific language governing permissions and limitations under the License.
*/

#include "pch.h"

#include "CCActionManager.h"
#include "CCScheduler.h"
#include "ccMacros.h"
#include "ccCArray.h"
#include "uthash.h"

NS_CC_BEGIN
//
// singleton stuff
//
static CCActionManager *gSharedManager = NULL;

typedef struct _hashElement
{
	struct _ccArray             *actions;
	CCObject					*target;
	unsigned int				actionIndex;
	CCAction					*currentAction;
	bool						currentActionSalvaged;
	bool						paused;
	UT_hash_handle				hh;
} tHashElement;

CCActionManager* CCActionManager::sharedManager(void)
{
	CCActionManager *pRet = gSharedManager;

	if (! gSharedManager)
	{
		pRet = gSharedManager = new CCActionManager();

		if (! gSharedManager->init())
		{
			// delete CCActionManager if init error
			delete gSharedManager;
			gSharedManager = NULL;
			pRet = NULL;
		}
	}

	return pRet;
}

void CCActionManager::purgeSharedManager(void)
{
	CCScheduler::sharedScheduler()->unscheduleUpdateForTarget(this);
	CC_SAFE_RELEASE(gSharedManager);
}

CCActionManager::CCActionManager(void)
: m_pTargets(NULL), 
  m_pCurrentTarget(NULL),
  m_bCurrentTargetSalvaged(false)
{
	CCAssert(gSharedManager == NULL, "");
}

CCActionManager::~CCActionManager(void)
{
	CCLOGINFO("cocos2d: deallocing %p", this);

	removeAllActions();

	// ?? do not delete , is it because purgeSharedManager() delete it? 
	gSharedManager = NULL;
}

bool CCActionManager::init(void)
{
	CCScheduler::sharedScheduler()->scheduleUpdateForTarget(this, 0, false);
	m_pTargets = NULL;

	return true;
}

// private

void CCActionManager::deleteHashElement(tHashElement *pElement)
{
	ccArrayFree(pElement->actions);
	HASH_DEL(m_pTargets, pElement);
	pElement->target->release();
	free(pElement);
}

void CCActionManager::actionAllocWithHashElement(tHashElement *pElement)
{
	// 4 actions per Node by default
	if (pElement->actions == NULL)
	{
		pElement->actions = ccArrayNew(4);
	}else 
	if (pElement->actions->num == pElement->actions->max)
	{
		ccArrayDoubleCapacity(pElement->actions);
	}

}

void CCActionManager::removeActionAtIndex(unsigned int uIndex, tHashElement *pElement)
{
	CCAction *pAction = (CCAction*)pElement->actions->arr[uIndex];

	if (pAction == pElement->currentAction && (! pElement->currentActionSalvaged))
	{
		pElement->currentAction->retain();
		pElement->currentActionSalvaged = true;
	}

	ccArrayRemoveObjectAtIndex(pElement->actions, uIndex);

	// update actionIndex in case we are in tick. looping over the actions
	if (pElement->actionIndex >= uIndex)
	{
		pElement->actionIndex--;
	}

	if (pElement->actions->num == 0)
	{
		if (m_pCurrentTarget == pElement)
		{
			m_bCurrentTargetSalvaged = true;
		}
		else
		{
			deleteHashElement(pElement);
		}
	}
}

// pause / resume

void CCActionManager::pauseTarget(CCObject *pTarget)
{
	tHashElement *pElement = NULL;
	HASH_FIND_INT(m_pTargets, &pTarget, pElement);
	if (pElement)
	{
		pElement->paused = true;
	}
}

void CCActionManager::resumeTarget(CCObject *pTarget)
{
	tHashElement *pElement = NULL;
	HASH_FIND_INT(m_pTargets, &pTarget, pElement);
	if (pElement)
	{
		pElement->paused = false;
	}
}

// run

void CCActionManager::addAction(CCAction *pAction, CCNode *pTarget, bool paused)
{
	CCAssert(pAction != NULL, "");
	CCAssert(pTarget != NULL, "");

	tHashElement *pElement = NULL;
	// we should convert it to CCObject*, because we save it as CCObject*
	CCObject *tmp = pTarget;
	HASH_FIND_INT(m_pTargets, &tmp, pElement);
	if (! pElement)
	{
		pElement = (tHashElement*)calloc(sizeof(*pElement), 1);
		pElement->paused = paused;
		pTarget->retain();
		pElement->target = pTarget;
		HASH_ADD_INT(m_pTargets, target, pElement);
	}

 	actionAllocWithHashElement(pElement);
 
 	CCAssert(! ccArrayContainsObject(pElement->actions, pAction), "");
 	ccArrayAppendObject(pElement->actions, pAction);
 
 	pAction->startWithTarget(pTarget);
}

// remove

void CCActionManager::removeAllActions(void)
{
	for (tHashElement *pElement = m_pTargets; pElement != NULL; )
	{
		CCObject *pTarget = pElement->target;
		pElement = (tHashElement*)pElement->hh.next;
		removeAllActionsFromTarget(pTarget);
	}
}

void CCActionManager::removeAllActionsFromTarget(CCObject *pTarget)
{
	// explicit null handling
	if (pTarget == NULL)
	{
		return;
	}

	tHashElement *pElement = NULL;
	HASH_FIND_INT(m_pTargets, &pTarget, pElement);
	if (pElement)
	{
		if (ccArrayContainsObject(pElement->actions, pElement->currentAction) && (! pElement->currentActionSalvaged))
		{
			pElement->currentAction->retain();
			pElement->currentActionSalvaged = true;
		}

		ccArrayRemoveAllObjects(pElement->actions);
		if (m_pCurrentTarget == pElement)
		{
			m_bCurrentTargetSalvaged = true;
		}
		else
		{
			deleteHashElement(pElement);
		}
	}
	else
	{
//		CCLOG("cocos2d: removeAllActionsFromTarget: Target not found");
	}
}

void CCActionManager::removeAction(CCAction *pAction)
{
	// explicit null handling
	if (pAction == NULL)
	{
		return;
	}

	tHashElement *pElement = NULL;
	CCObject *pTarget = pAction->getOriginalTarget();
	HASH_FIND_INT(m_pTargets, &pTarget, pElement);
	if (pElement)
	{
		unsigned int i = ccArrayGetIndexOfObject(pElement->actions, pAction);
		if (UINT_MAX != i)
		{
			removeActionAtIndex(i, pElement);
		}
	}
	else
	{
		CCLOG("cocos2d: removeAction: Target not found");
	}
}

void CCActionManager::removeActionByTag(unsigned int tag, CCObject *pTarget)
{
    CCAssert((int)tag != kCCActionTagInvalid, "");
	CCAssert(pTarget != NULL, "");

	tHashElement *pElement = NULL;
	HASH_FIND_INT(m_pTargets, &pTarget, pElement);

	if (pElement)
	{
		unsigned int limit = pElement->actions->num;
		for (unsigned int i = 0; i < limit; ++i)
		{
			CCAction *pAction = (CCAction*)pElement->actions->arr[i];

            if (pAction->getTag() == (int)tag && pAction->getOriginalTarget() == pTarget)
			{
				removeActionAtIndex(i, pElement);
				break;
			}
		}
	}
}

// get

CCAction* CCActionManager::getActionByTag(unsigned int tag, CCObject *pTarget)
{
    CCAssert((int)tag != kCCActionTagInvalid, "");

	tHashElement *pElement = NULL;
	HASH_FIND_INT(m_pTargets, &pTarget, pElement);

	if (pElement)
	{
		if (pElement->actions != NULL)
		{
			unsigned int limit = pElement->actions->num;
			for (unsigned int i = 0; i < limit; ++i)
			{
				CCAction *pAction = (CCAction*)pElement->actions->arr[i];

                if (pAction->getTag() == (int)tag)
				{
					return pAction;
				}
			}
		}
		CCLOG("cocos2d : getActionByTag: Action not found");
	}
	else
	{
        CCLOG("cocos2d : getActionByTag: Target not found");
	}

	return NULL;
}

unsigned int CCActionManager::numberOfRunningActionsInTarget(CCObject *pTarget)
{
	tHashElement *pElement = NULL;
	HASH_FIND_INT(m_pTargets, &pTarget, pElement);
	if (pElement)
	{
		return pElement->actions ? pElement->actions->num : 0;
	}

	return 0;
}

// main loop
void CCActionManager::update(ccTime dt)
{
	for (tHashElement *elt = m_pTargets; elt != NULL; )
	{
		m_pCurrentTarget = elt;
		m_bCurrentTargetSalvaged = false;

		if (! m_pCurrentTarget->paused)
		{
			// The 'actions' CCMutableArray may change while inside this loop.
			for (m_pCurrentTarget->actionIndex = 0; m_pCurrentTarget->actionIndex < m_pCurrentTarget->actions->num;
				m_pCurrentTarget->actionIndex++)
			{
				m_pCurrentTarget->currentAction = (CCAction*)m_pCurrentTarget->actions->arr[m_pCurrentTarget->actionIndex];
				if (m_pCurrentTarget->currentAction == NULL)
				{
					continue;
				}

				m_pCurrentTarget->currentActionSalvaged = false;

				m_pCurrentTarget->currentAction->step(dt);

				if (m_pCurrentTarget->currentActionSalvaged)
				{
					// The currentAction told the node to remove it. To prevent the action from
					// accidentally deallocating itself before finishing its step, we retained
					// it. Now that step is done, it's safe to release it.
					m_pCurrentTarget->currentAction->release();
				} else
				if (m_pCurrentTarget->currentAction->isDone())
				{
					m_pCurrentTarget->currentAction->stop();

					CCAction *pAction = m_pCurrentTarget->currentAction;
					// Make currentAction nil to prevent removeAction from salvaging it.
					m_pCurrentTarget->currentAction = NULL;
					removeAction(pAction);
				}

				m_pCurrentTarget->currentAction = NULL;
			}
		}

		// elt, at this moment, is still valid
		// so it is safe to ask this here (issue #490)
		elt = (tHashElement*)(elt->hh.next);

		// only delete currentTarget if no actions were scheduled during the cycle (issue #481)
		if (m_bCurrentTargetSalvaged && m_pCurrentTarget->actions->num == 0)
		{
			deleteHashElement(m_pCurrentTarget);
		}
	}

	// issue #635
	m_pCurrentTarget = NULL;
}

NS_CC_END