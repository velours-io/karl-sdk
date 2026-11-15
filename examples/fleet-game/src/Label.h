#pragma once

#include <karl/sdk.h>

#include <osg/PositionAttitudeTransform>
#include <osgText/Text>

#include <string>

namespace fleet
{
    // A line of text in KARL's look (Host::makeText), placed by its anchor.
    class Label : public osg::PositionAttitudeTransform
    {
    public:
        Label(const karl::sdk::Host *host, const std::string &text, karl::sdk::Font font = karl::sdk::Font::BOLD)
            : _host(host), _value(text)
        {
            _text = host->makeText(host, text.c_str(), 60, font);
            this->addChild(_text);
        }

        void setText(const std::string &text)
        {
            if (text == _value)
            {
                return;
            }
            _value = text;
            _text->setText(text, osgText::String::ENCODING_UTF8);
        }
        const std::string &getText() const { return _value; }

        void setFont(karl::sdk::Font font) { _text->setFont(_host->fontPath(_host, font)); }
        void setCharacterSize(float size) { _text->setCharacterSize(size); }
        void setAlignment(osgText::Text::AlignmentType alignment) { _text->setAlignment(alignment); }
        void setColor(const osg::Vec4 &color) { _text->setColor(color); }
        void setPos(float x, float y) { this->setPosition(osg::Vec3(x, y, 0)); }
        void setPos(const osg::Vec2 &pos) { setPos(pos.x(), pos.y()); }

    private:
        const karl::sdk::Host *_host;
        osg::ref_ptr<osgText::Text> _text;
        std::string _value;
    };
}
