#pragma once
// Copyright (c) 2016, PG, All rights reserved.

#include "CarouselButton.h"

class CollectionButton final : public CarouselButton {
    NOCOPY_NOMOVE(CollectionButton)
   public:
    CollectionButton(float xPos, float yPos, float xSize, float ySize, UString name, const UString &collectionName,
                     std::vector<SongButton *> children = {});
    ~CollectionButton() override = default;

    void draw() override;

    void triggerContextMenu(vec2 pos);

    [[nodiscard]] Color getActiveBackgroundColor() const override;
    [[nodiscard]] Color getInactiveBackgroundColor() const override;

    [[nodiscard]] const std::string &getCollectionName() const { return this->sCollectionName; }
    void setCollectionName(std::string_view newName) { this->sCollectionName = newName; }

    [[nodiscard]] i32 getNumVisibleChildren() const { return this->numVisibleChildren; }
    void setNumVisibleChildren(i32 numVis) { this->numVisibleChildren = numVis; }

   private:
    void onSelected(bool wasSelected, SelOpts opts) override;
    void onRightMouseUpInside() override;

    void onContextMenu(const UString &text, int id = -1);
    void onRenameCollectionConfirmed(const UString &text, int id = -1);
    void onDeleteCollectionConfirmed(const UString &text, int id = -1);

    std::string sCollectionName;

    float fTitleScale;
    i32 numVisibleChildren{0};
};
