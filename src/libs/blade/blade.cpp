/******************************************************************************
File:  blade.cpp

Author:   Nick Chirkov

Comments:
model binded to an animated locator
******************************************************************************/
#include "blade.h"
#include "../../Shared/messages.h"
#include "geometry.h"
static const char *handName = "Saber_hand";
static const char *beltName = "Saber_belt";
static const char *bloodName = "Saber_blood";
static const char *lightName = "Saber_light";
static const char *bladeStart = "start";
static const char *bladeEnd = "end";

static const char *gunHandName = "gun_hand";
static const char *gunBeltName = "gun_belt";
static const char *gunFire = "gun_fire";

static const char *sabergunHandName = "sabergun_hand";
static const char *sabergunBeltName = "sabergun_belt";
static const char *sabergunFire = "sabersabgun_fire";

BLADE::BLADE_INFO::BLADE_INFO()
{
    locatorName = beltName;
    defLifeTime = 0.15f;
    lifeTime = 0.0f;
    color[0] = 0x80162EBE;
    color[1] = 0x00FF0000;
    time = 0.0f;
    for (long v = 0; v < WAY_LENGTH; v++)
        vrtTime[v] = -1e20f;
}

BLADE::BLADE_INFO::~BLADE_INFO()
{
    EntityManager::EraseEntity(eid);
}

void BLADE::BLADE_INFO::DrawBlade(VDX9RENDER *rs, unsigned int blendValue, MODEL *mdl, NODE *manNode)
{
    auto *obj = static_cast<MODEL *>(EntityManager::GetEntityPointer(eid));
    if (obj != nullptr)
    {
        CMatrix perMtx;

        auto *bladeNode = obj->GetNode(0);
        if ((blendValue & 0xff000000) == 0xff000000)
        {
            bladeNode->SetTechnique("EnvAmmoShader");
        }
        else
        {
            bladeNode->SetTechnique("AnimationBlend");
        }
        long sti = -1;
        auto idBlade = manNode->geo->FindName(locatorName);

        if ((sti = manNode->geo->FindLabelN(sti + 1, idBlade)) > -1)
        {
            auto *ani = mdl->GetAnimation();
            auto *bones = &ani->GetAnimationMatrix(0);

            GEOS::LABEL lb;
            manNode->geo->GetLabel(sti, lb);
            CMatrix mt;
            mt.Vx() = CVECTOR(lb.m[0][0], lb.m[0][1], lb.m[0][2]);
            mt.Vy() = CVECTOR(lb.m[1][0], lb.m[1][1], lb.m[1][2]);
            mt.Vz() = CVECTOR(lb.m[2][0], lb.m[2][1], lb.m[2][2]);
            mt.Pos() = CVECTOR(lb.m[3][0], lb.m[3][1], lb.m[3][2]);

            auto mbn = mt * bones[lb.bones[0]];
            mbn.Pos().x *= -1.0f;
            mbn.Vx().x *= -1.0f;
            mbn.Vy().x *= -1.0f;
            mbn.Vz().x *= -1.0f;

            CMatrix scl;
            scl.Vx().x = -1.0f;
            scl.Vy().y = 1.0f;
            scl.Vz().z = 1.0f;
            mbn.EqMultiply(scl, CMatrix(mbn));

            perMtx = mbn * mdl->mtx;
        }
        obj->mtx = perMtx;
        obj->ProcessStage(Stage::realize, 0);

        //--------------------------------------------------------------------------
        rs->SetTexture(0, nullptr);
        rs->SetTransform(D3DTS_WORLD, CMatrix());

        // move to the beginning
        long first = 0; // end vertex 2 draw
        for (long v = WAY_LENGTH - 2; v > -1; v--)
        {
            vrt[v * 2 + 2] = vrt[v * 2 + 0];
            vrt[v * 2 + 3] = vrt[v * 2 + 1];
            vrtTime[v + 1] = vrtTime[v + 0];

            if (vrtTime[v + 1] + lifeTime <= time)
                continue;

            first++;

            auto blend = (time - vrtTime[v + 1]) / lifeTime;

            auto fcol0 = static_cast<float>((color[0] >> 24) & 0xFF);
            auto fcol1 = static_cast<float>((color[1] >> 24) & 0xFF);
            auto a = static_cast<unsigned long>(fcol0 + blend * (fcol1 - fcol0));

            fcol0 = static_cast<float>((color[0] >> 16) & 0xFF);
            fcol1 = static_cast<float>((color[1] >> 16) & 0xFF);
            auto r = static_cast<unsigned long>(fcol0 + blend * (fcol1 - fcol0));

            fcol0 = static_cast<float>((color[0] >> 8) & 0xFF);
            fcol1 = static_cast<float>((color[1] >> 8) & 0xFF);
            auto g = static_cast<unsigned long>(fcol0 + blend * (fcol1 - fcol0));

            fcol0 = static_cast<float>((color[0] >> 0) & 0xFF);
            fcol1 = static_cast<float>((color[1] >> 0) & 0xFF);
            auto b = static_cast<unsigned long>(fcol0 + blend * (fcol1 - fcol0));

            vrt[v * 2 + 2].diffuse = vrt[v * 2 + 3].diffuse = (a << 24) | (r << 16) | (g << 8) | b;
        }

        // search for start
        sti = bladeNode->geo->FindLabelN(0, bladeNode->geo->FindName(bladeStart));
        GEOS::LABEL lb;
        if (sti >= 0)
        {
            bladeNode->geo->GetLabel(sti, lb);
            vrt[0].pos = perMtx * CVECTOR(lb.m[3][0], lb.m[3][1], lb.m[3][2]);
            vrt[0].diffuse = color[0];
            sti = bladeNode->geo->FindLabelN(0, bladeNode->geo->FindName(bladeEnd));
            if (sti >= 0)
            {
                bladeNode->geo->GetLabel(sti, lb);
                vrt[1].pos = perMtx * CVECTOR(lb.m[3][0], lb.m[3][1], lb.m[3][2]);
                vrt[1].diffuse = color[0];
                vrtTime[0] = time;

                auto bDraw = rs->TechniqueExecuteStart("Blade");
                if (bDraw)
                {
                    if (first > 0)
                        rs->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, FVF, first * 2, &vrt[0], sizeof vrt[0]);
                }
                while (rs->TechniqueExecuteNext())
                {
                }
            }
            else
            {
                core.Trace("BLADE::Realize -> no find locator \"%s\", model \"%s\"", bladeEnd, bladeNode->GetName());
            }
        }
        else
        {
            core.Trace("BLADE::Realize -> no find locator \"%s\", model \"%s\"", bladeStart, bladeNode->GetName());
        }
    }
}

bool BLADE::BLADE_INFO::LoadBladeModel(MESSAGE &message)
{
    EntityManager::EraseEntity(eid);

    // model name
    char mdlName[200];
    message.String(sizeof(mdlName), mdlName);
    mdlName[sizeof(mdlName) - 1] = 0;
    if (mdlName[0] != '\0')
    {
        // path of the model
        char path[256];
        strcpy_s(path, "Ammo\\");
        strcat_s(path, mdlName);
        // path of the textures
        auto *gs = static_cast<VGEOMETRY *>(core.CreateService("geometry"));
        if (gs)
            gs->SetTexturePath("Ammo\\");
        // Create a model
        eid = EntityManager::CreateEntity("modelr");
        if (!core.Send_Message(eid, "ls", MSG_MODEL_LOAD_GEO, path))
        {
            EntityManager::EraseEntity(eid);
            if (gs)
                gs->SetTexturePath("");
            return false;
        }
        if (gs)
            gs->SetTexturePath("");
        // trace parameters
        defLifeTime = message.Float();
        color[0] = message.Long();
        color[1] = message.Long();
    }
    else
        return false;
    return true;
}

BLADE::BLADE()
{
    gunLocName = gunBeltName;
    blendValue = 0xFFFFFFFF;
}

BLADE::~BLADE()
{
    EntityManager::EraseEntity(gun);

    for (long i = 0; i < ITEMS_INFO_QUANTITY; i++)
        items[i].Release();
}

bool BLADE::Init()
{
    // GUARD(BLADE::BLADE())

    col = static_cast<COLLIDE *>(core.CreateService("coll"));
    if (col == nullptr)
        throw std::exception("No service: COLLIDE");

    EntityManager::AddToLayer(REALIZE, GetId(), 65550);

    rs = static_cast<VDX9RENDER *>(core.CreateService("dx9render"));
    if (!rs)
        throw std::exception("No service: dx9render");

    // UNGUARD
    return true;
}

//------------------------------------------------------------------------------------
// realize
//------------------------------------------------------------------------------------
void BLADE::Realize(uint32_t Delta_Time)
{
    blade[0].time += 0.001f * (Delta_Time);

    auto *mdl = static_cast<MODEL *>(EntityManager::GetEntityPointer(man));
    if (!mdl)
        return;

    auto *manNode = mdl->GetNode(0);
    rs->TextureSet(0, -1);
    rs->TextureSet(1, -1);
    rs->TextureSet(2, -1);
    rs->TextureSet(3, -1);

    CMatrix mtx;
    rs->GetTransform(D3DTS_VIEW, mtx);
    mtx.Transposition();
    mtx.Pos() = 0.0f;
    rs->SetTransform(D3DTS_TEXTURE1, mtx);
    rs->SetRenderState(D3DRS_TEXTUREFACTOR, blendValue);

    //------------------------------------------------------
    // draw gun
    CMatrix perMtx;
    long sti;
    auto *obj = static_cast<MODEL *>(EntityManager::GetEntityPointer(gun));
    if (obj != nullptr)
    {
        auto *gunNode = obj->GetNode(0);
        if ((blendValue & 0xff000000) == 0xff000000)
        {
            gunNode->SetTechnique("EnvAmmoShader");
        }
        else
        {
            gunNode->SetTechnique("AnimationBlend");
        }
        sti = -1;
        auto idGun = manNode->geo->FindName(gunLocName);

        if ((sti = manNode->geo->FindLabelN(sti + 1, idGun)) > -1)
        {
            auto *ani = mdl->GetAnimation();
            auto *bones = &ani->GetAnimationMatrix(0);

            GEOS::LABEL lb;
            manNode->geo->GetLabel(sti, lb);
            CMatrix mt;
            mt.Vx() = CVECTOR(lb.m[0][0], lb.m[0][1], lb.m[0][2]);
            mt.Vy() = CVECTOR(lb.m[1][0], lb.m[1][1], lb.m[1][2]);
            mt.Vz() = CVECTOR(lb.m[2][0], lb.m[2][1], lb.m[2][2]);
            mt.Pos() = CVECTOR(lb.m[3][0], lb.m[3][1], lb.m[3][2]);

            auto mbn = mt * bones[lb.bones[0]];
            mbn.Pos().x *= -1.0f;
            mbn.Vx().x *= -1.0f;
            mbn.Vy().x *= -1.0f;
            mbn.Vz().x *= -1.0f;

            CMatrix scl;
            scl.Vx().x = -1.0f;
            scl.Vy().y = 1.0f;
            scl.Vz().z = 1.0f;
            mbn.EqMultiply(scl, CMatrix(mbn));

            perMtx = mbn * mdl->mtx;
        }
        obj->mtx = perMtx;

        obj->ProcessStage(Stage::realize, 0);
    }

    //------------------------------------------------------
    // draw saber
    blade[0].DrawBlade(rs, blendValue, mdl, manNode);
    blade[1].DrawBlade(rs, blendValue, mdl, manNode);

    //------------------------------------------------------
    // draw tied items
    for (long n = 0; n < ITEMS_INFO_QUANTITY; n++)
        if (items[n].nItemIndex != -1)
            items[n].DrawItem(rs, blendValue, mdl, manNode);

    mtx.SetIdentity();
    rs->SetTransform(D3DTS_TEXTURE1, mtx);
}

bool BLADE::LoadBladeModel(MESSAGE &message)
{
    const auto nBladeIdx = message.Long();
    if (nBladeIdx < 0 || nBladeIdx >= BLADE_INFO_QUANTITY)
        return false;

    man = message.EntityID();

    if (nBladeIdx == 1)
        blade[nBladeIdx].locatorName = sabergunBeltName;
    else
        blade[nBladeIdx].locatorName = beltName;

    return blade[nBladeIdx].LoadBladeModel(message);
}

bool BLADE::LoadGunModel(MESSAGE &message)
{
    EntityManager::EraseEntity(gun);
    man = message.EntityID();
    // model name
    char mdlName[200];
    message.String(sizeof(mdlName), mdlName);
    mdlName[sizeof(mdlName) - 1] = 0;
    if (mdlName[0] != '\0')
    {
        // path of the model
        char path[256];
        strcpy_s(path, "Ammo\\");
        strcat_s(path, mdlName);
        // path of the textures
        auto *gs = static_cast<VGEOMETRY *>(core.CreateService("geometry"));
        if (gs)
            gs->SetTexturePath("Ammo\\");
        // Create a model
        gun = EntityManager::CreateEntity("modelr");
        if (!core.Send_Message(gun, "ls", MSG_MODEL_LOAD_GEO, path))
        {
            EntityManager::EraseEntity(gun);
            if (gs)
                gs->SetTexturePath("");
            return false;
        }
        if (gs)
            gs->SetTexturePath("");
    }
    else
        return false;
    return true;
}

void BLADE::GunFire()
{
    auto *mdl = static_cast<MODEL *>(EntityManager::GetEntityPointer(man));
    auto *manNode = mdl->GetNode(0);

    //------------------------------------------------------
    // search gunfire
    CMatrix perMtx;
    long sti;

    auto *obj = static_cast<MODEL *>(EntityManager::GetEntityPointer(gun));
    if (obj == nullptr) // no pistol - look for saber pistol
        obj = static_cast<MODEL *>(EntityManager::GetEntityPointer(blade[1].eid));

    if (obj != nullptr)
    {
        auto *gunNode = obj->GetNode(0);
        sti = -1;
        auto idGun = manNode->geo->FindName(gunLocName);

        if ((sti = manNode->geo->FindLabelN(sti + 1, idGun)) > -1)
        {
            auto *ani = mdl->GetAnimation();
            auto *bones = &ani->GetAnimationMatrix(0);

            GEOS::LABEL lb;
            manNode->geo->GetLabel(sti, lb);
            CMatrix mt;
            mt.Vx() = CVECTOR(lb.m[0][0], lb.m[0][1], lb.m[0][2]);
            mt.Vy() = CVECTOR(lb.m[1][0], lb.m[1][1], lb.m[1][2]);
            mt.Vz() = CVECTOR(lb.m[2][0], lb.m[2][1], lb.m[2][2]);
            mt.Pos() = CVECTOR(lb.m[3][0], lb.m[3][1], lb.m[3][2]);

            auto mbn = mt * bones[lb.bones[0]];
            mbn.Pos().x *= -1.0f;
            mbn.Vx().x *= -1.0f;
            mbn.Vy().x *= -1.0f;
            mbn.Vz().x *= -1.0f;
            perMtx = mbn * mdl->mtx;
        }

        // search for start
        sti = gunNode->geo->FindLabelN(0, gunNode->geo->FindName(gunFire));
        if (sti >= 0)
        {
            GEOS::LABEL lb;
            gunNode->geo->GetLabel(sti, lb);
            CMatrix resm;
            resm.EqMultiply(perMtx, *(CMatrix *)&lb.m);
            auto rp = perMtx * CVECTOR(lb.m[3][0], lb.m[3][1], lb.m[3][2]);

            core.Send_Message(EntityManager::GetEntityId("particles"), "lsffffffl", PS_CREATEX, "gunfire", rp.x, rp.y,
                              rp.z, resm.Vz().x, resm.Vz().y, resm.Vz().z, 0);
        }
        else
            core.Trace("MSG_BLADE_GUNFIRE Can't find gun_fire locator");
    }
}

uint64_t BLADE::ProcessMessage(MESSAGE &message)
{
    long n;

    switch (message.Long())
    {
    case MSG_BLADE_SET:
        return LoadBladeModel(message);
        break;

    case MSG_BLADE_BELT:
        n = message.Long();
        if (n == 0)
        {
            blade[n].locatorName = beltName;
            blade[n].lifeTime = 0.0f;
        }
        else if (n == 1)
        {
            blade[n].locatorName = sabergunBeltName;
            blade[n].lifeTime = 0.0f;
        }
        // core.Trace("MSG_BLADE_BELT::%s", beltName);
        break;

    case MSG_BLADE_HAND:
        n = message.Long();
        if (n == 0)
        {
            blade[n].locatorName = handName;
        }
        else if (n == 1)
        {
            blade[n].locatorName = sabergunHandName;
        }
        // core.Trace("MSG_BLADE_HAND::%s", handName);
        break;

    case MSG_BLADE_GUNSET:
        return LoadGunModel(message);
        break;
    case MSG_BLADE_GUNBELT:
        gunLocName = gunBeltName;
        // core.Trace("MSG_BLADE_GUNBELT::%s", gunLocName);
        break;
    case MSG_BLADE_GUNHAND:
        gunLocName = gunHandName;
        // core.Trace("MSG_BLADE_GUNHAND::%s", gunLocName);
        break;
    case MSG_BLADE_GUNFIRE:
        GunFire();
        // core.Trace("MSG_BLADE_GUNFIRE::%s", handName);
        break;

    case MSG_BLADE_TRACE_ON:
        n = message.Long();
        if (n >= 0 && n < BLADE_INFO_QUANTITY)
        {
            blade[0].lifeTime = blade[0].defLifeTime;
        }
        // core.Trace("MSG_BLADE_TRACE_ON::%f", lifeTime);
        break;

    case MSG_BLADE_TRACE_OFF:
        n = message.Long();
        if (n >= 0 && n < BLADE_INFO_QUANTITY)
        {
            blade[0].lifeTime = 0.0f;
        }
        // core.Trace("MSG_BLADE_TRACE_OFF");
        break;

    case MSG_BLADE_BLOOD:
        // core.Trace("MSG_BLADE_BLOOD");
        break;

    case MSG_BLADE_LIGHT:
        // core.Trace("MSG_BLADE_LIGHT");
        break;
    case MSG_BLADE_ALPHA:
        blendValue = message.Long();
        break;

    case 1001:
        man = message.EntityID();
        AddTieItem(message);
        break;
    case 1002:
        DelTieItem(message);
        break;
    case 1003:
        DelAllTieItem();
        break;
    }
    return 0;
}

void BLADE::AddTieItem(MESSAGE &message)
{
    const auto nItemIdx = message.Long();

    char mdlName[MAX_PATH];
    message.String(sizeof(mdlName), mdlName);

    char locName[128];
    message.String(sizeof(locName), locName);

    auto n = FindTieItemByIndex(nItemIdx);
    if (n >= 0)
    {
        core.Trace("Warning! BLADE::AddTieItem(%d,%s,%s) already set that item", nItemIdx, mdlName, locName);
    }
    else
    {
        for (n = 0; n < ITEMS_INFO_QUANTITY; n++)
            if (items[n].nItemIndex == -1)
                break;
        if (n < ITEMS_INFO_QUANTITY)
        {
            items[n].nItemIndex = nItemIdx;
            items[n].LoadItemModel(mdlName, locName);
        }
        else
        {
            core.Trace("Warning! BLADE::AddTieItem(%d,%s,%s) very mach items already set", nItemIdx, mdlName, locName);
        }
    }
}

void BLADE::DelTieItem(MESSAGE &message)
{
    const auto nItemIdx = message.Long();
    const long n = FindTieItemByIndex(nItemIdx);
    if (n >= 0)
        items[n].Release();
}

void BLADE::DelAllTieItem()
{
    for (long i = 0; i < ITEMS_INFO_QUANTITY; i++)
        if (items[i].nItemIndex != -1)
            items[i].Release();
}

long BLADE::FindTieItemByIndex(long n)
{
    if (n < 0)
        return -1;
    for (long i = 0; i < ITEMS_INFO_QUANTITY; i++)
        if (items[i].nItemIndex == n)
            return i;
    return -1;
}

void BLADE::TIEITEM_INFO::Release()
{
    if (nItemIndex != -1)
    {
        nItemIndex = -1;
        EntityManager::EraseEntity(eid);
        delete locatorName;
        locatorName = nullptr;
    }
}

void BLADE::TIEITEM_INFO::DrawItem(VDX9RENDER *rs, unsigned int blendValue, MODEL *mdl, NODE *manNode)
{
    auto *obj = static_cast<MODEL *>(EntityManager::GetEntityPointer(eid));
    if (obj != nullptr)
    {
        CMatrix perMtx;

        NODE *mdlNode = obj->GetNode(0);
        if ((blendValue & 0xff000000) == 0xff000000)
        {
            mdlNode->SetTechnique("EnvAmmoShader");
        }
        else
        {
            mdlNode->SetTechnique("AnimationBlend");
        }
        long sti = -1;
        auto idLoc = manNode->geo->FindName(locatorName);

        if ((sti = manNode->geo->FindLabelN(sti + 1, idLoc)) > -1)
        {
            Animation *ani = mdl->GetAnimation();
            CMatrix *bones = &ani->GetAnimationMatrix(0);

            GEOS::LABEL lb;
            manNode->geo->GetLabel(sti, lb);
            CMatrix mt;
            mt.Vx() = CVECTOR(lb.m[0][0], lb.m[0][1], lb.m[0][2]);
            mt.Vy() = CVECTOR(lb.m[1][0], lb.m[1][1], lb.m[1][2]);
            mt.Vz() = CVECTOR(lb.m[2][0], lb.m[2][1], lb.m[2][2]);
            mt.Pos() = CVECTOR(lb.m[3][0], lb.m[3][1], lb.m[3][2]);

            CMatrix mbn = mt * bones[lb.bones[0]];
            mbn.Pos().x *= -1.0f;
            mbn.Vx().x *= -1.0f;
            mbn.Vy().x *= -1.0f;
            mbn.Vz().x *= -1.0f;

            CMatrix scl;
            scl.Vx().x = -1.0f;
            scl.Vy().y = 1.0f;
            scl.Vz().z = 1.0f;
            mbn.EqMultiply(scl, CMatrix(mbn));

            perMtx = mbn * mdl->mtx;
        }
        obj->mtx = perMtx;
        obj->ProcessStage(Stage::realize, 0);
    }
}

bool BLADE::TIEITEM_INFO::LoadItemModel(const char *mdlName, const char *locName)
{
    EntityManager::EraseEntity(eid);
    delete locatorName;
    locatorName = nullptr;

    if (!locName || !mdlName)
        return false;

    const auto len = strlen(locName) + 1;
    locatorName = new char[len];
    Assert(locatorName);
    memcpy(locatorName, locName, len);

    // path of the model
    char path[256];
    strcpy_s(path, "Ammo\\");
    strcat_s(path, mdlName);
    // path of the textures
    auto *gs = static_cast<VGEOMETRY *>(core.CreateService("geometry"));
    if (gs)
        gs->SetTexturePath("Ammo\\");
    // Create a model
    eid = EntityManager::CreateEntity("modelr");
    if (!core.Send_Message(eid, "ls", MSG_MODEL_LOAD_GEO, path))
    {
        EntityManager::EraseEntity(eid);
        if (gs)
            gs->SetTexturePath("");
        return false;
    }
    if (gs)
        gs->SetTexturePath("");

    return true;
}