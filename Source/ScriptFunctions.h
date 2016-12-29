// Common functions for server, client, mapper
// Works in scripts compiler

template< class T >
static Entity* EntityDownCast( T* a )
{
    if( !a )
        return nullptr;
    Entity* b = (Entity*) a;
    b->AddRef();
    return b;
}

template< class T >
static T* EntityUpCast( Entity* a )
{
    if( !a )
        return nullptr;
    #define CHECK_CAST( cast_class, entity_type )                                         \
        if( std::is_same< T, cast_class >::value && (EntityType) a->Type == entity_type ) \
        {                                                                                 \
            T* b = (T*) a;                                                                \
            b->AddRef();                                                                  \
            return b;                                                                     \
        }
    #if defined ( FONLINE_SERVER )
    CHECK_CAST( Location, EntityType::Location );
    CHECK_CAST( Map, EntityType::Map );
    CHECK_CAST( Critter, EntityType::Npc );
    CHECK_CAST( Critter, EntityType::Client );
    CHECK_CAST( Item, EntityType::Item );
    CHECK_CAST( Item, EntityType::ItemHex );
    #elif defined ( FONLINE_CLIENT ) || defined ( FONLINE_MAPPER )
    CHECK_CAST( Location, EntityType::Location );
    CHECK_CAST( Map, EntityType::Map );
    CHECK_CAST( CritterCl, EntityType::CritterCl );
    CHECK_CAST( Item, EntityType::Item );
    CHECK_CAST( Item, EntityType::ItemHex );
    #endif
    #undef CHECK_CAST
    return nullptr;
}

static void Global_Assert( bool condition )
{
    if( !condition )
        Script::RaiseException( "Assertion failed" );
}

static void Global_ThrowException( string message )
{
    Script::RaiseException( "%s", message.c_str() );
}

static int Global_Random( int min, int max )
{
    static Randomizer script_randomizer;
    return script_randomizer.Random( min, max );
}

static void Global_Log( string text )
{
    #ifndef FONLINE_SCRIPT_COMPILER
    Script::Log( text.c_str() );
    #else
    printf( "%s\n", text.c_str() );
    #endif
}

static bool Global_StrToInt( string text, int& result )
{
    if( text.empty() || !Str::IsNumber( text.c_str() ) )
        return false;
    result = Str::AtoI( text.c_str() );
    return true;
}

static bool Global_StrToFloat( string text, float& result )
{
    if( text.empty() )
        return false;
    result = (float) strtod( text.c_str(), nullptr );
    return true;
}

static uint Global_GetDistantion( ushort hx1, ushort hy1, ushort hx2, ushort hy2 )
{
    return DistGame( hx1, hy1, hx2, hy2 );
}

static uchar Global_GetDirection( ushort from_hx, ushort from_hy, ushort to_hx, ushort to_hy )
{
    return GetFarDir( from_hx, from_hy, to_hx, to_hy );
}

static uchar Global_GetOffsetDir( ushort from_hx, ushort from_hy, ushort to_hx, ushort to_hy, float offset )
{
    return GetFarDir( from_hx, from_hy, to_hx, to_hy, offset );
}

static uint Global_GetTick()
{
    return Timer::FastTick();
}

static uint Global_GetAngelScriptProperty( int property )
{
    return (uint) asGetActiveContext()->GetEngine()->GetEngineProperty( (asEEngineProp) property );
}

static bool Global_SetAngelScriptProperty( int property, uint value )
{
    return asGetActiveContext()->GetEngine()->SetEngineProperty( (asEEngineProp) property, value ) >= 0;
}

static hash Global_GetStrHash( string str )
{
    if( str.empty() )
        return 0;
    return Str::GetHash( str.c_str() );
}

static string Global_GetHashStr( hash h )
{
    if( !h )
        return "";
    const char* str = Str::GetName( h );
    return str ? str : "";
}

static uint Global_DecodeUTF8( string text, uint& length )
{
    return Str::DecodeUTF8( text.c_str(), &length );
}

static string Global_EncodeUTF8( uint ucs )
{
    char buf[ 5 ];
    uint len = Str::EncodeUTF8( ucs, buf );
    buf[ len ] = 0;
    return buf;
}

static uint Global_GetFolderFileNames( string path, string ext, bool include_subdirs, CScriptArray* result )
{
    StrVec files;
    FileManager::GetFolderFileNames( path.c_str(), include_subdirs, !ext.empty() ? ext.c_str() : nullptr, files );

    if( result )
    {
        for( auto& f : files )
            result->InsertLast( &f );
    }

    return (uint) files.size();
}

static bool Global_DeleteFile( string filename )
{
    return FileManager::DeleteFile( filename.c_str() );
}

static void Global_CreateDirectoryTree( string path )
{
    char tmp[ MAX_FOPATH ];
    Str::Copy( tmp, path.c_str() );
    Str::Append( tmp, "/" );
    FileManager::FormatPath( tmp );
    CreateDirectoryTree( tmp );
}

static void Global_Yield( uint time )
{
    #ifndef FONLINE_SCRIPT_COMPILER
    Script::SuspendCurrentContext( time );
    #endif
}

static string Global_SHA1( string text )
{
    #ifndef FONLINE_SCRIPT_COMPILER
    SHA1_CTX ctx;
    SHA1_Init( &ctx );
    SHA1_Update( &ctx, (uchar*) text.c_str(), text.length() );
    uchar digest[ SHA1_DIGEST_SIZE ];
    SHA1_Final( &ctx, digest );

    static const char* nums = "0123456789abcdef";
    char               hex_digest[ SHA1_DIGEST_SIZE * 2 ];
    for( uint i = 0; i < sizeof( hex_digest ); i++ )
        hex_digest[ i ] = nums[ i % 2 ? digest[ i / 2 ] & 0xF : digest[ i / 2 ] >> 4 ];
    return string( hex_digest, sizeof( hex_digest ) );
    #else
    return "";
    #endif
}

static string Global_SHA2( string text )
{
    #ifndef FONLINE_SCRIPT_COMPILER
    const uint digest_size = 32;
    uchar      digest[ digest_size ];
    sha256( (uchar*) text.c_str(), (uint) text.length(), digest );

    static const char* nums = "0123456789abcdef";
    char               hex_digest[ digest_size * 2 ];
    for( uint i = 0; i < sizeof( hex_digest ); i++ )
        hex_digest[ i ] = nums[ i % 2 ? digest[ i / 2 ] & 0xF : digest[ i / 2 ] >> 4 ];
    return string( hex_digest, sizeof( hex_digest ) );
    #else
    return nullptr;
    #endif
}

static void Global_OpenLink( string link )
{
    #ifdef FO_WINDOWS
    ShellExecuteW( nullptr, L"open", CharToWideChar( link ).c_str(), nullptr, nullptr, SW_SHOWNORMAL );
    #else
    system( ( string( "xdg-open " ) + link ).c_str() );
    #endif
}

static Item* Global_GetProtoItem( hash pid, CScriptDict* props )
{
    #ifndef FONLINE_SCRIPT_COMPILER
    ProtoItem* proto = ProtoMngr.GetProtoItem( pid );
    if( !proto )
        return nullptr;

    Item* item = new Item( 0, proto );
    if( props )
    {
        for( asUINT i = 0; i < props->GetSize(); i++ )
        {
            if( !Properties::SetValueAsIntProps( (Properties*) &item->Props, *(int*) props->GetKey( i ), *(int*) props->GetValue( i ) ) )
            {
                item->Release();
                return nullptr;
            }
        }
    }
    return item;
    #else
    return nullptr;
    #endif
}
