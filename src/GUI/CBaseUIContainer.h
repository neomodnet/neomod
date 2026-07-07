#pragma once
// Copyright (c) 2011, PG, All rights reserved.
#include "CBaseUIElement.h"

#include <span>

class CBaseUIContainer : public CBaseUIElement {
    NOCOPY_NOMOVE(CBaseUIContainer)
   public:
    CBaseUIContainer(float xPos = 0, float yPos = 0, float xSize = 0, float ySize = 0, std::string name = {});
    ~CBaseUIContainer() override;

    virtual void freeElements();
    virtual void invalidate();

    void draw_debug();
    void draw() override;
    void tick() override;
    void updateInput(CBaseUIEventCtx &c) override;

    void onKeyUp(KeyboardEvent &e) override;
    void onKeyDown(KeyboardEvent &e) override;
    void onChar(KeyboardEvent &e) override;

    CBaseUIContainer *addBaseUIElement(CBaseUIElement *element, float xPos, float yPos);
    CBaseUIContainer *addBaseUIElement(CBaseUIElement *element, vec2 pos);
    CBaseUIContainer *addBaseUIElement(CBaseUIElement *element);
    CBaseUIContainer *addBaseUIElements(const std::vector<CBaseUIElement *> &elements);
    CBaseUIContainer *addBaseUIElements(std::span<CBaseUIElement *const> elements);

    CBaseUIContainer *addBaseUIElementBack(CBaseUIElement *element, float xPos, float yPos);
    CBaseUIContainer *addBaseUIElementBack(CBaseUIElement *element, vec2 pos);
    CBaseUIContainer *addBaseUIElementBack(CBaseUIElement *element);

    CBaseUIContainer *insertBaseUIElement(CBaseUIElement *element, CBaseUIElement *index);
    CBaseUIContainer *insertBaseUIElementBack(CBaseUIElement *element, CBaseUIElement *index);

    CBaseUIContainer *removeBaseUIElement(CBaseUIElement *element);
    CBaseUIContainer *deleteBaseUIElement(CBaseUIElement *element);

    CBaseUIElement *getBaseUIElement(std::string_view name);

    bool isBusy() override;
    bool isActive() override;

    void onMoved() override { this->update_pos(); }
    void onResized() override { this->update_pos(); }

    void onMouseDownOutside(bool left = true, bool right = false) override;

    void onFocusStolen() override;
    void onEnabled() override;
    void onDisabled() override;

    void update_pos();

    [[nodiscard]] forceinline const std::vector<CBaseUIElement *> &getElements() const { return this->vElements; }

    // don't use this blindly, make sure that you haven't added anything that isn't compatible with T to the container!
    template <typename T>
    [[nodiscard]] forceinline const std::vector<T *> &getElementsAs() const
        requires(std::derived_from<T, CBaseUIElement>)
    {
        return reinterpret_cast<const std::vector<T *> &>(this->vElements);
    }

    [[nodiscard]] std::span<CBaseUIElement *const> getAllChildren() const override;

   protected:
    std::vector<CBaseUIElement *> vElements;
};
