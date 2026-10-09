#include "CollisionComponent.h"

const char* CollisionComponent::g_Name = "CollisionComponent";

bool CollisionComponent::VInit(TiXmlElement* data)
{
    assert(data != NULL);

    TiXmlElement* collisionSizeElement = data->FirstChildElement("CollisionSize");
    if (collisionSizeElement != NULL)
    {
        int w = 0, h = 0;
        collisionSizeElement->Attribute("width", &w);
        collisionSizeElement->Attribute("height", &h);
        _collisionWidth = w;
        _collisionHeight = h;
    }
    else
    {
        return false;
    }

    return true;
}

TiXmlElement* CollisionComponent::VGenerateXml()
{
    TiXmlElement* baseElement = new TiXmlElement(VGetName());

    TiXmlElement* collisionSizeElement = new TiXmlElement("CollisionSize");
    collisionSizeElement->SetAttribute("width", std::to_string(_collisionWidth).c_str());
    collisionSizeElement->SetAttribute("height", std::to_string(_collisionHeight).c_str());
    baseElement->LinkEndChild(collisionSizeElement);

    return baseElement;
}