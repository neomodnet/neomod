#pragma once
// Copyright (c) 2011, PG, All rights reserved.
#include "CBaseUIElement.h"

#include <span>

class CBaseUIContainer : public CBaseUIElement {
    NOCOPY_NOMOVE(CBaseUIContainer)
   public:
    CBaseUIContainer(float xPos = 0, float yPos = 0, float xSize = 0, float ySize = 0, UString name = {});
    ~CBaseUIContainer() override;

    virtual void freeElements();
    virtual void invalidate();

    void draw_debug();
    void draw() override;
    void update(CBaseUIEventCtx &c) override;

    void onKeyUp(KeyboardEvent &e) override;
    void onKeyDown(KeyboardEvent &e) override;
    void onChar(KeyboardEvent &e) override;

    CBaseUIContainer *addBaseUIElement(CBaseUIElement *element, float xPos, float yPos);
    inline CBaseUIContainer *addBaseUIElement(CBaseUIElement *element, vec2 pos) {
        return this->addBaseUIElement(element, pos.x, pos.y);
    }
    CBaseUIContainer *addBaseUIElement(CBaseUIElement *element);
    CBaseUIContainer *addBaseUIElements(const std::vector<CBaseUIElement *> &elements);
    inline CBaseUIContainer *addBaseUIElements(std::span<CBaseUIElement *> elements) {
        return this->addBaseUIElements(std::vector<CBaseUIElement *>{elements.begin(), elements.end()});
    }

    CBaseUIContainer *addBaseUIElementBack(CBaseUIElement *element, float xPos, float yPos);
    inline CBaseUIContainer *addBaseUIElementBack(CBaseUIElement *element, vec2 pos) {
        return this->addBaseUIElementBack(element, pos.x, pos.y);
    }
    CBaseUIContainer *addBaseUIElementBack(CBaseUIElement *element);

    CBaseUIContainer *insertBaseUIElement(CBaseUIElement *element, CBaseUIElement *index);
    CBaseUIContainer *insertBaseUIElementBack(CBaseUIElement *element, CBaseUIElement *index);

    CBaseUIContainer *removeBaseUIElement(CBaseUIElement *element);
    CBaseUIContainer *deleteBaseUIElement(CBaseUIElement *element);

    CBaseUIElement *getBaseUIElement(const UString &name);

    bool isBusy() override;
    bool isActive() override;

    void onMoved() override { this->update_pos(); }
    void onResized() override { this->update_pos(); }

    void onMouseDownOutside(bool left = true, bool right = false) override;

    void onFocusStolen() override;
    void onEnabled() override;
    void onDisabled() override;

    void update_pos();

    // don't use this blindly, make sure that you haven't added anything that isn't compatible with T to the container!
    template <typename T = CBaseUIElement>
    [[nodiscard]] forceinline const std::vector<T *> &getElements() const
        requires(std::derived_from<T, CBaseUIElement>)
    {
        return reinterpret_cast<const std::vector<T *> &>(this->vElements);
    }

   protected:
    std::vector<CBaseUIElement *> vElements;
};
