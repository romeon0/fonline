#include "ItemManager.h"
#include "ProtoManager.h"
#include "CritterManager.h"
#include "MapManager.h"
#include "EntityManager.h"

ItemManager ItemMngr;

void ItemManager::GetGameItems( ItemVec& items )
{
    EntityMngr.GetItems( items );
}

uint ItemManager::GetItemsCount()
{
    return EntityMngr.GetEntitiesCount( EntityType::Item );
}

void ItemManager::SetCritterItems( Critter* cr )
{
    ItemVec items;
    EntityMngr.GetCritterItems( cr->GetId(), items );

    for( auto it = items.begin(); it != items.end(); ++it )
    {
        Item* item = *it;
        SYNC_LOCK( item );

        cr->SetItem( item );
        if( item->GetIsRadio() )
            RadioRegister( item, true );
    }
}

Item* ItemManager::CreateItem( hash pid, uint count /* = 0 */, Properties* props /* = nullptr */ )
{
    ProtoItem* proto = ProtoMngr.GetProtoItem( pid );
    if( !proto )
    {
        WriteLogF( _FUNC_, " - Proto item '%s' not found.\n", Str::GetName( pid ) );
        return nullptr;
    }

    Item* item = new Item( 0, proto );
    if( props )
        item->Props = *props;
    SYNC_LOCK( item );

    // Main collection
    EntityMngr.RegisterEntity( item );

    // Count
    if( count )
        item->SetCount( count );

    // Radio collection
    if( item->GetIsRadio() )
        RadioRegister( item, true );

    // Prototype script
    item->SetScript( nullptr, true );

    // Verify destroying
    if( item->IsDestroyed )
    {
        WriteLogF( _FUNC_, " - Item destroyed after prototype '%s' initialization.\n", Str::GetName( pid ) );
        return nullptr;
    }

    return item;
}

bool ItemManager::RestoreItem( uint id, hash proto_id, const StrMap& props_data )
{
    ProtoItem* proto = ProtoMngr.GetProtoItem( proto_id );
    if( !proto )
    {
        WriteLog( "Proto item '%s' is not loaded.\n", Str::GetName( proto_id ) );
        return false;
    }

    Item* item = new Item( id, proto );
    if( !item->Props.LoadFromText( props_data ) )
    {
        WriteLog( "Fail to restore properties for item '%s' (%u).\n", Str::GetName( proto_id ), id );
        item->Release();
        return false;
    }

    SYNC_LOCK( item );
    EntityMngr.RegisterEntity( item );
    return true;
}

void ItemManager::DeleteItem( Item* item )
{
    // Synchronize
    SYNC_LOCK( item );

    // Redundant calls
    if( item->IsDestroying || item->IsDestroyed )
        return;
    item->IsDestroying = true;

    // Finish events
    item->EventFinish( true );

    // Delete children
    for( int i = 0; i < ITEM_MAX_CHILDS; i++ )
    {
        Item* child = item->GetChild( i );
        if( child )
            DeleteItem( child );
    }

    // Tear off from environment
    while( item->GetAccessory() != ITEM_ACCESSORY_NONE || item->ContIsItems() )
    {
        // Delete from owner
        EraseItemHolder( item );

        // Delete child items
        if( item->ContIsItems() )
            item->ContDeleteItems();
    }

    // Erase from statistics
    ChangeItemStatistics( item->GetProtoId(), -(int) item->GetCount() );

    // Erase from radio collection
    if( item->GetIsRadio() )
        RadioRegister( item, false );

    // Erase from main collection
    EntityMngr.UnregisterEntity( item );

    // Invalidate for use
    item->IsDestroyed = true;

    // Release after some time
    Job::DeferredRelease( item );
}

Item* ItemManager::SplitItem( Item* item, uint count )
{
    if( !item->IsStackable() )
    {
        WriteLogF( _FUNC_, " - Splitted item '%s' is not stackable, id %u.\n", item->GetName(), item->GetId() );
        return nullptr;
    }

    uint item_count = item->GetCount();
    if( !count || count >= item_count )
    {
        WriteLogF( _FUNC_, " - Invalid item '%s' count, id %u, count %u, split count %u.\n", item->GetName(), item->GetId(), item_count, count );
        return nullptr;
    }

    Item* new_item = CreateItem( item->GetProtoId(), count, &item->Props ); // Ignore init script
    if( !new_item )
    {
        WriteLogF( _FUNC_, " - Create item '%s' fail, count %u.\n", item->GetName(), count );
        return nullptr;
    }

    item->ChangeCount( -(int) count );

    // Radio collection
    if( new_item->GetIsRadio() )
        RadioRegister( new_item, true );

    return new_item;
}

Item* ItemManager::GetItem( uint item_id, bool sync_lock )
{
    Item* item = (Item*) EntityMngr.GetEntity( item_id, EntityType::Item );
    if( item && sync_lock )
        SYNC_LOCK( item );
    return item;
}

void ItemManager::EraseItemHolder( Item* item )
{
    switch( item->GetAccessory() )
    {
    case ITEM_ACCESSORY_CRITTER:
    {
        Critter* cr = CrMngr.GetCritter( item->GetCritId(), true );
        if( cr )
            cr->EraseItem( item, true );
        else if( item->GetIsRadio() )
            ItemMngr.RadioRegister( item, true );
        item->SetCritId( 0 );
        item->SetCritSlot( 0 );
    }
    break;
    case ITEM_ACCESSORY_HEX:
    {
        Map* map = MapMngr.GetMap( item->GetMapId(), true );
        if( map )
            map->EraseItem( item->GetId() );
        item->SetMapId( 0 );
    }
    break;
    case ITEM_ACCESSORY_CONTAINER:
    {
        Item* cont = ItemMngr.GetItem( item->GetContainerId(), true );
        if( cont )
            cont->ContEraseItem( item );
        item->SetContainerId( 0 );
        item->SetContainerStack( 0 );
    }
    break;
    default:
        break;
    }
    item->SetAccessory( ITEM_ACCESSORY_NONE );
}

void ItemManager::MoveItem( Item* item, uint count, Critter* to_cr )
{
    if( item->GetAccessory() == ITEM_ACCESSORY_CRITTER && item->GetCritId() == to_cr->GetId() )
        return;

    if( count >= item->GetCount() || !item->IsStackable() )
    {
        EraseItemHolder( item );
        to_cr->AddItem( item, true );
    }
    else
    {
        Item* item_ = ItemMngr.SplitItem( item, count );
        if( item_ )
            to_cr->AddItem( item_, true );
    }
}

void ItemManager::MoveItem( Item* item, uint count, Map* to_map, ushort to_hx, ushort to_hy )
{
    if( item->GetAccessory() == ITEM_ACCESSORY_HEX && item->GetMapId() == to_map->GetId() && item->GetHexX() == to_hx && item->GetHexY() == to_hy )
        return;

    if( count >= item->GetCount() || !item->IsStackable() )
    {
        EraseItemHolder( item );
        to_map->AddItem( item, to_hx, to_hy );
    }
    else
    {
        Item* item_ = ItemMngr.SplitItem( item, count );
        if( item_ )
            to_map->AddItem( item_, to_hx, to_hy );
    }
}

void ItemManager::MoveItem( Item* item, uint count, Item* to_cont, uint stack_id )
{
    if( item->GetAccessory() == ITEM_ACCESSORY_CONTAINER && item->GetContainerId() == to_cont->GetId() && item->GetContainerStack() == stack_id )
        return;

    if( count >= item->GetCount() || !item->IsStackable() )
    {
        EraseItemHolder( item );
        to_cont->ContAddItem( item, stack_id );
    }
    else
    {
        Item* item_ = ItemMngr.SplitItem( item, count );
        if( item_ )
            to_cont->ContAddItem( item_, stack_id );
    }
}

Item* ItemManager::AddItemContainer( Item* cont, hash pid, uint count, uint stack_id )
{
    RUNTIME_ASSERT( cont );

    Item* item = cont->ContGetItemByPid( pid, stack_id );
    Item* result = nullptr;

    if( item )
    {
        if( item->IsStackable() )
        {
            item->ChangeCount( count );
            result = item;
        }
        else
        {
            if( count > MAX_ADDED_NOGROUP_ITEMS )
                count = MAX_ADDED_NOGROUP_ITEMS;
            for( uint i = 0; i < count; ++i )
            {
                item = ItemMngr.CreateItem( pid );
                if( !item )
                    continue;
                cont->ContAddItem( item, stack_id );
                result = item;
            }
        }
    }
    else
    {
        ProtoItem* proto_item = ProtoMngr.GetProtoItem( pid );
        if( !proto_item )
            return result;

        if( proto_item->GetStackable() )
        {
            item = ItemMngr.CreateItem( pid, count );
            if( !item )
                return result;
            cont->ContAddItem( item, stack_id );
            result = item;
        }
        else
        {
            if( count > MAX_ADDED_NOGROUP_ITEMS )
                count = MAX_ADDED_NOGROUP_ITEMS;
            for( uint i = 0; i < count; ++i )
            {
                item = ItemMngr.CreateItem( pid );
                if( !item )
                    continue;
                cont->ContAddItem( item, stack_id );
                result = item;
            }
        }
    }

    return result;
}

Item* ItemManager::AddItemCritter( Critter* cr, hash pid, uint count )
{
    if( !count )
        return nullptr;

    Item* item = cr->GetItemByPid( pid );
    Item* result = nullptr;

    if( item )
    {
        if( item->IsStackable() )
        {
            item->ChangeCount( count );
            result = item;
        }
        else
        {
            if( count > MAX_ADDED_NOGROUP_ITEMS )
                count = MAX_ADDED_NOGROUP_ITEMS;
            for( uint i = 0; i < count; ++i )
            {
                item = ItemMngr.CreateItem( pid );
                if( !item )
                    break;
                cr->AddItem( item, true );
                result = item;
            }
        }
    }
    else
    {
        ProtoItem* proto_item = ProtoMngr.GetProtoItem( pid );
        if( !proto_item )
            return result;

        if( proto_item->GetStackable() )
        {
            item = ItemMngr.CreateItem( pid, count );
            if( !item )
                return result;
            cr->AddItem( item, true );
            result = item;
        }
        else
        {
            if( count > MAX_ADDED_NOGROUP_ITEMS )
                count = MAX_ADDED_NOGROUP_ITEMS;
            for( uint i = 0; i < count; ++i )
            {
                item = ItemMngr.CreateItem( pid );
                if( !item )
                    break;
                cr->AddItem( item, true );
                result = item;
            }
        }
    }

    return result;
}

bool ItemManager::SubItemCritter( Critter* cr, hash pid, uint count, ItemVec* erased_items )
{
    if( !count )
        return true;

    Item* item = cr->GetItemByPidInvPriority( pid );
    if( !item )
        return true;

    if( item->IsStackable() )
    {
        if( count >= item->GetCount() )
        {
            cr->EraseItem( item, true );
            if( !erased_items )
                ItemMngr.DeleteItem( item );
            else
                erased_items->push_back( item );
        }
        else
        {
            if( erased_items )
            {
                Item* item_ = ItemMngr.SplitItem( item, count );
                if( item_ )
                    erased_items->push_back( item_ );
            }
            else
            {
                item->ChangeCount( -(int) count );
            }
        }
    }
    else
    {
        for( uint i = 0; i < count; ++i )
        {
            cr->EraseItem( item, true );
            if( !erased_items )
                ItemMngr.DeleteItem( item );
            else
                erased_items->push_back( item );
            item = cr->GetItemByPidInvPriority( pid );
            if( !item )
                return true;
        }
    }

    return true;
}

bool ItemManager::SetItemCritter( Critter* cr, hash pid, uint count )
{
    uint cur_count = cr->CountItemPid( pid );
    if( cur_count > count )
        return SubItemCritter( cr, pid, cur_count - count );
    else if( cur_count < count )
        return AddItemCritter( cr, pid, count - cur_count ) != nullptr;
    return true;
}

bool ItemManager::MoveItemCritters( Critter* from_cr, Critter* to_cr, uint item_id, uint count )
{
    Item* item = from_cr->GetItem( item_id, false );
    if( !item )
        return false;

    if( !count || count > item->GetCount() )
        count = item->GetCount();

    if( item->IsStackable() && item->GetCount() > count )
    {
        Item* item_ = to_cr->GetItemByPid( item->GetProtoId() );
        if( !item_ )
        {
            item_ = ItemMngr.CreateItem( item->GetProtoId(), count );
            if( !item_ )
            {
                WriteLogF( _FUNC_, " - Create item '%s' fail.\n", item->GetName() );
                return false;
            }

            to_cr->AddItem( item_, true );
        }
        else
        {
            item_->ChangeCount( count );
        }

        item->ChangeCount( -(int) count );
    }
    else
    {
        from_cr->EraseItem( item, true );
        to_cr->AddItem( item, true );
    }

    return true;
}

bool ItemManager::MoveItemCritterToCont( Critter* from_cr, Item* to_cont, uint item_id, uint count, uint stack_id )
{
    Item* item = from_cr->GetItem( item_id, false );
    if( !item )
        return false;

    if( !count || count > item->GetCount() )
        count = item->GetCount();

    if( item->IsStackable() && item->GetCount() > count )
    {
        Item* item_ = to_cont->ContGetItemByPid( item->GetProtoId(), stack_id );
        if( !item_ )
        {
            item_ = ItemMngr.CreateItem( item->GetProtoId(), count );
            if( !item_ )
            {
                WriteLogF( _FUNC_, " - Create item '%s' fail.\n", item->GetName() );
                return false;
            }

            item_->SetContainerStack( stack_id );
            to_cont->ContSetItem( item_ );
        }
        else
        {
            item_->ChangeCount( count );
        }

        item->ChangeCount( -(int) count );
    }
    else
    {
        from_cr->EraseItem( item, true );
        to_cont->ContAddItem( item, stack_id );
    }

    return true;
}

bool ItemManager::MoveItemCritterFromCont( Item* from_cont, Critter* to_cr, uint item_id, uint count )
{
    Item* item = from_cont->ContGetItem( item_id, false );
    if( !item )
        return false;

    if( !count || count > item->GetCount() )
        count = item->GetCount();

    if( item->IsStackable() && item->GetCount() > count )
    {
        Item* item_ = to_cr->GetItemByPid( item->GetProtoId() );
        if( !item_ )
        {
            item_ = ItemMngr.CreateItem( item->GetProtoId(), count );
            if( !item_ )
            {
                WriteLogF( _FUNC_, " - Create item '%s' fail.\n", item->GetName() );
                return false;
            }

            to_cr->AddItem( item_, true );
        }
        else
        {
            item_->ChangeCount( count );
        }

        item->ChangeCount( -(int) count );
    }
    else
    {
        from_cont->ContEraseItem( item );
        to_cr->AddItem( item, true );
    }

    return true;
}

bool ItemManager::MoveItemsContainers( Item* from_cont, Item* to_cont, uint from_stack_id, uint to_stack_id )
{
    if( !from_cont->ContIsItems() )
        return true;

    ItemVec items;
    from_cont->ContGetItems( items, from_stack_id, true );
    for( auto it = items.begin(), end = items.end(); it != end; ++it )
    {
        Item* item = *it;
        from_cont->ContEraseItem( item );
        to_cont->ContAddItem( item, to_stack_id );
    }

    return true;
}

bool ItemManager::MoveItemsContToCritter( Item* from_cont, Critter* to_cr, uint stack_id )
{
    if( !from_cont->ContIsItems() )
        return true;

    ItemVec items;
    from_cont->ContGetItems( items, stack_id, true );
    for( auto it = items.begin(), end = items.end(); it != end; ++it )
    {
        Item* item = *it;
        from_cont->ContEraseItem( item );
        to_cr->AddItem( item, true );
    }

    return true;
}

void ItemManager::RadioClear()
{
    SCOPE_LOCK( radioItemsLocker );

    radioItems.clear();
}

void ItemManager::RadioRegister( Item* radio, bool add )
{
    SCOPE_LOCK( radioItemsLocker );

    auto it = std::find( radioItems.begin(), radioItems.end(), radio );

    if( add )
    {
        if( it == radioItems.end() )
            radioItems.push_back( radio );
    }
    else
    {
        if( it != radioItems.end() )
            radioItems.erase( it );
    }
}

void ItemManager::RadioSendText( Critter* cr, const char* text, ushort text_len, bool unsafe_text, ushort text_msg, uint num_str, UShortVec& channels )
{
    ItemVec radios;
    ItemVec items = cr->GetItemsNoLock();
    for( auto it = items.begin(), end = items.end(); it != end; ++it )
    {
        Item* item = *it;
        if( item->GetIsRadio() && item->RadioIsSendActive() &&
            std::find( channels.begin(), channels.end(), item->GetRadioChannel() ) == channels.end() )
        {
            channels.push_back( item->GetRadioChannel() );
            radios.push_back( item );

            if( radios.size() > 100 )
                break;
        }
    }

    for( uint i = 0, j = (uint) radios.size(); i < j; i++ )
    {
        RadioSendTextEx( channels[ i ],
                         radios[ i ]->GetRadioBroadcastSend(), cr->GetMapId(), cr->GetWorldX(), cr->GetWorldY(),
                         text, text_len, cr->IntellectCacheValue, unsafe_text, text_msg, num_str, nullptr );
    }
}

void ItemManager::RadioSendTextEx( ushort channel, int broadcast_type, uint from_map_id, ushort from_wx, ushort from_wy,
                                   const char* text, ushort text_len, ushort intellect, bool unsafe_text,
                                   ushort text_msg, uint num_str, const char* lexems )
{
    // Broadcast
    if( broadcast_type != RADIO_BROADCAST_FORCE_ALL && broadcast_type != RADIO_BROADCAST_WORLD &&
        broadcast_type != RADIO_BROADCAST_MAP && broadcast_type != RADIO_BROADCAST_LOCATION &&
        !( broadcast_type >= 101 && broadcast_type <= 200 ) /*RADIO_BROADCAST_ZONE*/ )
        return;
    if( ( broadcast_type == RADIO_BROADCAST_MAP || broadcast_type == RADIO_BROADCAST_LOCATION ) && !from_map_id )
        return;

    int  broadcast = 0;
    uint broadcast_map_id = 0;
    uint broadcast_loc_id = 0;

    // Get copy of all radios
    radioItemsLocker.Lock();
    ItemVec radio_items = radioItems;
    radioItemsLocker.Unlock();

    // Multiple sending controlling
    // Not thread safe, but this not so important in this case
    static uint msg_count = 0;
    msg_count++;

    // Send
    for( auto it = radio_items.begin(), end = radio_items.end(); it != end; ++it )
    {
        Item* radio = *it;

        if( radio->GetRadioChannel() == channel && radio->RadioIsRecvActive() )
        {
            if( broadcast_type != RADIO_BROADCAST_FORCE_ALL && radio->GetRadioBroadcastRecv() != RADIO_BROADCAST_FORCE_ALL )
            {
                if( broadcast_type == RADIO_BROADCAST_WORLD )
                    broadcast = radio->GetRadioBroadcastRecv();
                else if( radio->GetRadioBroadcastRecv() == RADIO_BROADCAST_WORLD )
                    broadcast = broadcast_type;
                else
                    broadcast = MIN( broadcast_type, radio->GetRadioBroadcastRecv() );

                if( broadcast == RADIO_BROADCAST_WORLD )
                    broadcast = RADIO_BROADCAST_FORCE_ALL;
                else if( broadcast == RADIO_BROADCAST_MAP || broadcast == RADIO_BROADCAST_LOCATION )
                {
                    if( !broadcast_map_id )
                    {
                        Map* map = MapMngr.GetMap( from_map_id, false );
                        if( !map )
                            continue;
                        broadcast_map_id = map->GetId();
                        broadcast_loc_id = map->GetLocation( false )->GetId();
                    }
                }
                else if( !( broadcast >= 101 && broadcast <= 200 ) /*RADIO_BROADCAST_ZONE*/ )
                    continue;
            }
            else
            {
                broadcast = RADIO_BROADCAST_FORCE_ALL;
            }

            if( radio->GetAccessory() == ITEM_ACCESSORY_CRITTER )
            {
                Client* cl = CrMngr.GetPlayer( radio->GetCritId(), false );
                if( cl && cl->RadioMessageSended != msg_count )
                {
                    if( broadcast != RADIO_BROADCAST_FORCE_ALL )
                    {
                        if( broadcast == RADIO_BROADCAST_MAP )
                        {
                            if( broadcast_map_id != cl->GetMapId() )
                                continue;
                        }
                        else if( broadcast == RADIO_BROADCAST_LOCATION )
                        {
                            Map* map = MapMngr.GetMap( cl->GetMapId(), false );
                            if( !map || broadcast_loc_id != map->GetLocation( false )->GetId() )
                                continue;
                        }
                        else if( broadcast >= 101 && broadcast <= 200 )                   // RADIO_BROADCAST_ZONE
                        {
                            if( !MapMngr.IsIntersectZone( from_wx, from_wy, 0, cl->GetWorldX(), cl->GetWorldY(), 0, broadcast - 101 ) )
                                continue;
                        }
                        else
                            continue;
                    }

                    if( text )
                        cl->Send_TextEx( radio->GetId(), text, text_len, SAY_RADIO, intellect, unsafe_text );
                    else if( lexems )
                        cl->Send_TextMsgLex( radio->GetId(), num_str, SAY_RADIO, text_msg, lexems );
                    else
                        cl->Send_TextMsg( radio->GetId(), num_str, SAY_RADIO, text_msg );

                    cl->RadioMessageSended = msg_count;
                }
            }
            else if( radio->GetAccessory() == ITEM_ACCESSORY_HEX )
            {
                if( broadcast == RADIO_BROADCAST_MAP && broadcast_map_id != radio->GetMapId() )
                    continue;

                Map* map = MapMngr.GetMap( radio->GetMapId(), false );
                if( map )
                {
                    if( broadcast != RADIO_BROADCAST_FORCE_ALL && broadcast != RADIO_BROADCAST_MAP )
                    {
                        if( broadcast == RADIO_BROADCAST_LOCATION )
                        {
                            Location* loc = map->GetLocation( false );
                            if( broadcast_loc_id != loc->GetId() )
                                continue;
                        }
                        else if( broadcast >= 101 && broadcast <= 200 )                   // RADIO_BROADCAST_ZONE
                        {
                            Location* loc = map->GetLocation( false );
                            if( !MapMngr.IsIntersectZone( from_wx, from_wy, 0, loc->GetWorldX(), loc->GetWorldY(), loc->GetRadius(), broadcast - 101 ) )
                                continue;
                        }
                        else
                            continue;
                    }

                    if( text )
                        map->SetText( radio->GetHexX(), radio->GetHexY(), 0xFFFFFFFE, text, text_len, intellect, unsafe_text );
                    else if( lexems )
                        map->SetTextMsgLex( radio->GetHexX(), radio->GetHexY(), 0xFFFFFFFE, text_msg, num_str, lexems, Str::Length( lexems ) );
                    else
                        map->SetTextMsg( radio->GetHexX(), radio->GetHexY(), 0xFFFFFFFE, text_msg, num_str );
                }
            }
        }
    }
}

void ItemManager::ChangeItemStatistics( hash pid, int val )
{
    SCOPE_LOCK( itemCountLocker );

    ProtoItem* proto = ProtoMngr.GetProtoItem( pid );
    if( proto )
        proto->InstanceCount += (int64) val;
}

int64 ItemManager::GetItemStatistics( hash pid )
{
    SCOPE_LOCK( itemCountLocker );

    ProtoItem* proto = ProtoMngr.GetProtoItem( pid );
    return proto ? proto->InstanceCount : 0;
}

string ItemManager::GetItemsStatistics()
{
    itemCountLocker.Lock();

    vector< ProtoItem* > protos;
    auto&                proto_items = ProtoMngr.GetProtoItems();
    protos.reserve( proto_items.size() );
    for( auto& kv : proto_items )
        protos.push_back( kv.second );

    itemCountLocker.Unlock();

    std::sort( protos.begin(), protos.end(), [] ( ProtoItem * p1, ProtoItem * p2 )
               {
                   return strcmp( p1->GetName(), p2->GetName() ) < 0;
               } );

    string result = "Name                                     Count\n";
    char   str[ MAX_FOTEXT ];
    for( auto it = protos.begin(), end = protos.end(); it != end; ++it )
    {
        ProtoItem* proto_item = *it;
        Str::Format( str, "%-40s %-20s\n", proto_item->GetName(), Str::I64toA( proto_item->InstanceCount ) );
        result += str;
    }
    return result;
}
