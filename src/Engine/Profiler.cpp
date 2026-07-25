// Copyright (c) 2020, PG, All rights reserved.
#include "Profiler.h"

#include "Engine.h"
#include "Timing.h"

ProfilerProfile g_profCurrentProfile(true);

// weird extra namespace here due to msvc linkage issues for callback in ConVarDefs.h
namespace Profiling {
void vprofToggleCB(float newValue) {
    const bool enable = !!static_cast<int>(newValue);

    if(enable != g_profCurrentProfile.isEnabled()) {
        if(enable)
            g_profCurrentProfile.start();
        else
            g_profCurrentProfile.stop();
    }
}
}  // namespace Profiling

ProfilerProfile::ProfilerProfile(bool manualStartViaMain) : root("Root", VPROF_BUDGETGROUP_ROOT, nullptr) {
    this->bManualStartViaMain = manualStartViaMain;

    this->iEnabled = 0;
    this->bEnableScheduled = false;
    this->bAtRoot = true;
    this->curNode = &this->root;

    this->iNumGroups = 0;
    for(auto &group : this->groups) {
        group.name = nullptr;
    }

    this->iNumNodes = 0;

    // create all groups in predefined order
    this->groupNameToID(
        VPROF_BUDGETGROUP_ROOT);  // NOTE: the root group must always be the first group to be created here
    if constexpr(Env::cfg(FEAT::MAINCB)) {
        this->groupNameToID(VPROF_BUDGETGROUP_BETWEENFRAMES);
    } else {
        // cannot (easily) be measured separately with main callbacks
        this->groupNameToID(VPROF_BUDGETGROUP_SLEEP);
        this->groupNameToID(VPROF_BUDGETGROUP_EVENTS);
    }
    this->groupNameToID(VPROF_BUDGETGROUP_UPDATE);

    this->groupNameToID(VPROF_BUDGETGROUP_DRAW);
    this->groupNameToID(VPROF_BUDGETGROUP_DRAW_SWAPBUFFERS);
}

double ProfilerProfile::sumTimes(const ProfilerNode *node, int groupID) const {
    if(node == nullptr) return 0.0;

    double sum = 0.0;

    const ProfilerNode *sibling = node;
    while(sibling != nullptr) {
        if(sibling->iGroupID == groupID) {
            if(sibling == &this->root)
                return (this->root.child != nullptr
                            ? this->root.child->fTimeLastFrame
                            : this->root.fTimeLastFrame);  // special case: early return for the root group (total
                                                           // duration of the entire frame)
            else
                return (sum + sibling->fTimeLastFrame);
        } else {
            if(sibling->child != nullptr) sum += this->sumTimes(sibling->child, groupID);
        }

        sibling = sibling->sibling;
    }

    return sum;
}

int ProfilerProfile::groupNameToID(const char *group) {
    if(this->iNumGroups >= VPROF_MAX_NUM_BUDGETGROUPS) {
        engine->showMessageErrorFatal("Engine", "Increase VPROF_MAX_NUM_BUDGETGROUPS");
        engine->shutdown();
        return -1;
    }

    for(int i = 0; i < this->iNumGroups; i++) {
        if(this->groups[i].name != nullptr && group != nullptr && strcmp(this->groups[i].name, group) == 0) return i;
    }

    const int newID = this->iNumGroups;
    this->groups[this->iNumGroups++].name = group;
    return newID;
}

int ProfilerNode::s_iNodeCounter = 0;

ProfilerNode::ProfilerNode() : ProfilerNode(nullptr, nullptr, nullptr) {}

ProfilerNode::ProfilerNode(const char *name, const char *group, ProfilerNode *parent) {
    this->constructor(name, group, parent);
}

void ProfilerNode::constructor(const char *name, const char *group, ProfilerNode *parent) {
    this->name = name;
    this->parent = parent;
    this->child = nullptr;
    this->sibling = nullptr;

    this->iNumRecursions = 0;
    this->fTime = 0.0;
    this->fTimeCurrentFrame = 0.0;
    this->fTimeLastFrame = 0.0;

    this->iGroupID = (s_iNodeCounter++ > 0 ? g_profCurrentProfile.groupNameToID(group) : 0);
}

void ProfilerNode::enterScope() {
    if(this->iNumRecursions++ == 0) {
        this->fTime = Timing::getTimeReal();
    }
}

bool ProfilerNode::exitScope() {
    if(--this->iNumRecursions == 0) {
        this->fTime = Timing::getTimeReal() - this->fTime;
        this->fTimeCurrentFrame = this->fTime;
    }

    return (this->iNumRecursions == 0);
}

ProfilerNode *ProfilerNode::getSubNode(const char *name, const char *group) {
    // find existing node
    ProfilerNode *child = this->child;
    while(child != nullptr) {
        if(child->name == name)  // NOTE: pointer comparison
            return child;

        child = child->sibling;
    }

    // "add" new node
    if(g_profCurrentProfile.iNumNodes >= VPROF_MAX_NUM_NODES) {
        engine->showMessageErrorFatal("Engine", "Increase VPROF_MAX_NUM_NODES");
        engine->shutdown();
        return nullptr;
    }

    ProfilerNode *node = &(g_profCurrentProfile.nodes[g_profCurrentProfile.iNumNodes++]);
    node->constructor(name, group, this);
    node->sibling = this->child;
    this->child = node;

    return node;
}
