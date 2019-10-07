//
// UILabel.h
//
// Clark Kromenaker
//
// A UI element that displays some text.
//
#pragma once
#include "UIWidget.h"

#include <string>
#include <vector>

#include "Material.h"
#include "TextLayout.h"

class Font;
class Mesh;

class UILabel : public UIWidget
{
	TYPE_DECL_CHILD();
public:
	UILabel(Actor* owner);
	
	void Render() override;
	
	void SetFont(Font* font);
	Font* GetFont() const { return mFont; }
	
	void SetHorizonalAlignment(HorizontalAlignment ha) { mHorizontalAlignment = ha; }
	void SetVerticalAlignment(VerticalAlignment va) { mVerticalAlignment = va; }
	
	void SetHorizontalOverflow(HorizontalOverflow ho) { mHorizontalOverflow = ho; }
	void SetVerticalOverflow(VerticalOverflow vo) { mVerticalOverflow = vo; }
	
	void SetText(std::string text);
	std::string GetText() const { return mText; }
	
protected:
	Vector2 GetCharPos(int index) const;
	Vector2 GetNextCharPos() const { return mTextLayout.GetNextCharPos(); }
	
	virtual void PopulateTextLayout(TextLayout& textLayout);
	
	void SetDirty() { mNeedMeshRegen = true; }
	
private:
	// The font used to display the label.
	Font* mFont = nullptr;
	
	// Desired text alignment within the transform's rect.
	HorizontalAlignment mHorizontalAlignment = HorizontalAlignment::Left;
	VerticalAlignment mVerticalAlignment = VerticalAlignment::Bottom;
	
	// Desired word wrap and overflow settings.
	HorizontalOverflow mHorizontalOverflow = HorizontalOverflow::Overflow;
	VerticalOverflow mVerticalOverflow = VerticalOverflow::Overflow;
	
	// The text to be displayed by the label.
	std::string mText;
	
	// Helper for laying out text within the available space with desired alignment/overflow.
	TextLayout mTextLayout;
	
	// Material used for rendering.
	Material mMaterial;
	
	// Mesh used for rendering.
	// This is generated from the desired text before rendering.
	Mesh* mMesh = nullptr;
	bool mNeedMeshRegen = false;
	
	void GenerateMesh();
};
