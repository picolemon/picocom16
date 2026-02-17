"""
Picocom16 resource packer.
- converts source image and audio to device formats.
- generates inline includes
- asset id fast lookup
- generates resource metadata for file offsets and storage locations

Asset database format:
- assets defined by a simple json descriptor
eg.
{   
    "outputType": "inline|bin"
    "outputName": "output.inl"
    "assets": {
        "assetname": { "id": "exportName", "type":"", ... }
        "assetdir/": { "type":"",... }
        "include": {"filename": "assets.json" }
    },
}

Asset ids ("id):
    Export asset into c/c++ eg. assetPack_assetId_..., symbol post fix is asset dependant eg.
     assetPack_assetId_data would contain byte stream, see generated inline headers for symbols.

Asset types ("type"):
    texture:
        Spec: {"format": "RGB16|RGBA16|1BPP"}
    ase_anim_texture:
        Spec: {"format": "RGB16|RGBA16|1BPP"}             
        Packs aseprite frames top to bottom, mem offset generated for each frame. To render get the mem offset and render as if it were a single frame, width stride is maintained etc.
        Ase support:
            - timeline frame
            - tags
            - pivots
            - layers

    font:
        Spec: {"format": "1BPP", "filetype": "bdf"}        
    dir:
        Spec: {"filename": "subAssets.json"}
        Desc: Add sub asset.json, allows inclusion of dep assets
    anim:
        Spec: ... name, frame:[]
        Desc: Define texture frames for animation playback
    data:
        Spec: ... userDataTypeId
        Desc: generic data buffer, embed bin data into asset pipeline for custom tooling        

    There are other undocumented type such as wave support, look through the examples for a better reference.
"""
import json, os, struct
from optparse import OptionParser
from bdfparser import Font  # pip install bdfparser
from PIL import Image 
import cstruct
from aseprite import *
import hashlib, os
import wave, struct, tempfile
import zlib
import model_converter


def compress( data, verify=False ):    
    """
    Max uncompressed block size = 4096 ( flash page size )
    Compression format:         
        BlockCount(int32_t)
        BlockSize(uint16) Compressed[ BlockSize... ]
        ... block count
    """
    # split into 4k blocks    
    offset = 0    

    # build block array
    blocks = []
    while offset < len(data):
        blockSz = 4096

        remain = len(data) - offset
        if remain < blockSz:
            blockSz = remain
        
        dataBlock = data[offset:offset+blockSz]

        # Compress block
        compressedBlock = zlib.compress( dataBlock )
        blocks.append( compressedBlock )
        
        offset += blockSz

    # build final stream
    result = bytearray()

    result += struct.pack("i", len(blocks))
    for block in blocks:

        result += struct.pack("H", len(block))
        result += block

    if verify:
        assert data == decompress( result ), "Failed to verify"

    return result


def compressList( data, verify=False ):    
    bytesData = compress( bytearray(data), verify )
    dataArrOut = []
    for i in bytesData:
        dataArrOut.append(i)
    return dataArrOut


def decompress( data ):
    offset = 0
    blockCnt = struct.unpack( "i", data[offset:offset+4])[0]
    offset += 4

    result = bytearray()
    for i in range(blockCnt):
        blockSz = struct.unpack( "H", data[offset:offset+2])[0]
        offset += 2

        blockData = data[offset:offset + blockSz]
        offset += blockSz
        decompressedData = zlib.decompress(blockData)
        result += decompressedData

    return result



def hashDir( dir ):
    hasher = hashlib.md5()
    for root, _,files in os.walk(dir, topdown=True):
        for name in files:            
            FileName = (os.path.join(root, name))            
            with open(str(FileName), 'rb') as afile:
                buf = afile.read()
                hasher.update(buf)
    return hasher.hexdigest()


#
#
def toArray( bytes ):
    res = []
    for i in bytes:
        res.append(i)
    return res


#
#
def encodeResourceId( packId, localResourceId ):
    return ((packId & 0xffff) << 16) | (localResourceId & 0xffff)


#
#
def generateCEmbedBytesArray( data, varname, externLink=False, addFastBuild=False ):
    """
        Generate flash embeded c array from data
    """    
    if externLink:
        return "extern uint8_t * __in_flash() %s_data;\n" % varname
    
    codeStr = "uint8_t __in_flash() %s_data[] = {\n" % varname

    if addFastBuild:
        codeStr += "#ifndef IMPL_EMBED_FAST_BUILD\n"
    bufferStr = ""
    maxCol = 32
    colCnt = 0
    for i in data:
        charInt = i
        charValStr = str(charInt)
        if bufferStr:
            bufferStr = bufferStr + ", "
        colCnt = colCnt + len(charValStr) + 2
        if colCnt > maxCol:
             bufferStr = bufferStr + "\n"
             colCnt = 0
        bufferStr = bufferStr + charValStr
    codeStr = codeStr + bufferStr

    if addFastBuild:
        codeStr += "\n#endif\n"

    codeStr = codeStr + "\n};\n"    
    codeStr = codeStr + "const int %s_size = %d;\n" % (varname, len(data)) # static size 
    return codeStr


def generateCEmbedIntArray( data, typeName, varname, externLink=False ):
    """
        Generate flash embeded c array from data
    """    
    if externLink:
        return "extern %s * __in_flash() %s_data[];\n" % (typeName,varname)
    
    codeStr = "%s __in_flash() %s_data[] = {\n" % (typeName,varname)

    bufferStr = ""
    maxCol = 32
    colCnt = 0
    for i in data:
        charInt = i
        charValStr = str(charInt)
        if bufferStr:
            bufferStr = bufferStr + ", "
        colCnt = colCnt + len(charValStr) + 2
        if colCnt > maxCol:
             bufferStr = bufferStr + "\n"
             colCnt = 0
        bufferStr = bufferStr + charValStr
    codeStr = codeStr + bufferStr
    codeStr = codeStr + "\n};\n"
    codeStr = codeStr + "const int %s_size = sizeof(%s_data);\n" % (varname, varname)
    return codeStr


#
#
class ResourcePackStream:
    """
        Packs resources into picocom readable format.
    """
    def __init__( s, packId, packSymbolName ):        
        s.packId = packId
        s.packSymbolName = packSymbolName # export name in c eg. packSymbolName_data, packSymbolName_size etc
        s.data = [] # each asset bytes section        
        s.compressData = []  # asset compression block data ( appended by endAsset serialiser )
        s.compressedData = [] # final compresstion block
        s.assetOffsets = []  # byte offset for each assets
        s.assetSymbolExport = {} # Export names and its index ( lookup via assetOffsets[ symbol ])
        s.compressed = False  # zlib compress entire pack


    def appendCompressionData( s, data ):

        offset = len( s.compressData )

        for i in data:
            s.compressData.append( i )
        
        return offset


    def addGfxResourceInfoPacker( s, info ):

        # call end handler        
        if s.compressed:
            info.endAsset( compressed=s )
        else:
            info.endAsset( compressed=None )

        if not info.data:
            print("[warn] addGfxResourceInfoPacker is empty for", info)
            return # null asset
        
        s.assetOffsets.append( len(s.data ) ) # Add offset
        s.data += info.data # add data

        # export name
        if info.exportSymbolName:
            s.assetSymbolExport[info.exportSymbolName] = len(s.assetOffsets)-1

        return len(s.assetOffsets) - 1


    def writeInlineHeader( s, outputFilename ):
        print("writeInlineHeader", outputFilename, len(s.data))

        codeStr = ''    

        # add headers        
        codeStr += "#pragma once\n"
        codeStr += "#include \"picocom/display/gfx.h\"\n\n"        
        codeStr += "#ifdef IMPL_EMBED_%(packSymbolName)s\n" % {'packSymbolName':s.packSymbolName}
        
        # export main pack data array
        codeStr = codeStr + generateCEmbedBytesArray( s.data, s.packSymbolName ) + "\n"

        # compress section into 4k blocks for uploader            
        s.compressedData = compress( bytearray( s.compressData ) )

        # gen compressed array
        codeStr = codeStr + generateCEmbedBytesArray( s.compressedData, s.packSymbolName + "_compressed", addFastBuild=True ) + "\n"

        # export resource id offsets
        codeStr = codeStr + generateCEmbedIntArray( s.assetOffsets, 'uint32_t', s.packSymbolName + "_offsets" ) + "\n"

        # build export names
        symbolExportNamesStr = ""
        for k, v in s.assetSymbolExport.items():
            symPrefix = "E%s_" % s.packSymbolName
            symbolExportNamesStr += "\t%s%s = 0x%x,\n" % (symPrefix,k, encodeResourceId( s.packId, localResourceId=v) )

        codeParams = {'packSymbolName':s.packSymbolName, 
           'assetCount': len(s.assetOffsets),
           'symbolExportNamesStr': symbolExportNamesStr,
           'packId': s.packId,           
           'compressed': int(s.compressed),
           'uncompressedSize': len(s.compressData)
           }

        # Asset info export
        packInfoExport = """const GfxAssetPack asset_%(packSymbolName)s = {
        .packId = %(packId)d,
        .size =  %(packSymbolName)s_size,
    .basePtr = %(packSymbolName)s_data,
    .compressedBasePtr = %(packSymbolName)s_compressed_data,
    .compressedSize =  %(packSymbolName)s_compressed_size,
    .uncompressedSize = %(uncompressedSize)d,
    .assetCount = %(assetCount)d,
    .assetOffsets = %(packSymbolName)s_offsets_data,
    .compressed = %(compressed)d,
#ifdef IMPL_EMBED_FAST_BUILD    
    .isInPlace = 1,
#else
    .isInPlace = 0,
#endif
}; 
""" % codeParams
        codeStr = codeStr + packInfoExport

        codeStr += "#else\n"
        

        # Asset info export
        codeStr = codeStr + """extern const GfxAssetPack asset_%(packSymbolName)s;\n""" % codeParams
                
        # extern export main pack data array
        codeStr = codeStr + generateCEmbedBytesArray( s.data, s.packSymbolName, externLink=True ) + "\n"

        # extern  export resource id offsets
        codeStr = codeStr + generateCEmbedIntArray( s.assetOffsets, 'uint32_t', s.packSymbolName + "_offsets", externLink=True ) + "\n"
        
        # extern compressed data
        if s.compressed:                        
            codeStr = codeStr + generateCEmbedBytesArray( [], s.packSymbolName + "_compressed", externLink=True ) + "\n"        
        
        codeStr += "#endif\n"
        
        # Gen enum
        codeStr += """enum E%(packSymbolName)sAssets {
%(symbolExportNamesStr)s
};\n""" % codeParams
        
        open(outputFilename,"w").write(codeStr)
        

    def writeBin( s, outputFilename ):
        print("writeBin", outputFilename, len(s.data))        

        # build dataPackHeader        
        #uint32_t packId;        // Unique pack id    
        #uint32_t size;          // Size of asset data    
        #uint32_t assetOffsetsCnt; // offsets/num assets
        #uint32_t assetOffsets[0]; // Resource offset map    
        dataPackHeader = struct.pack("III", s.packId, len(s.data), len(s.assetOffsets))
        for i in s.assetOffsets:
            dataPackHeader += struct.pack("I", i)

        open(outputFilename,"wb").write( dataPackHeader + bytearray(s.data))     


#
#
class EGfxAssetType:
    EGfxAssetType_None = 0
    EGfxAssetType_FontInfo = 1
    EGfxAssetType_Texture = 2
    EGfxAssetType_Mesh = 3
    EGfxAssetType_Spline = 4
    EGfxAssetType_AnimInfo = 5
    EGfxAssetType_PCM = 6
    EGfxAssetType_Ogg = 7
    EGfxAssetType_Custom = 128


#
#
class Texture:    
    Format1BPP = '1BPP'
    Format8BPP = '8BPP'
    FormatRGB16 = 'RGB16'
    FormatRGBA16 = 'RGBA16'    
    AllFormats = [Format1BPP, Format8BPP, FormatRGB16, FormatRGBA16]


class ETextureFormat:
    ETextureFormat_None = 0
    ETextureFormat_1BPP = 1            #// 1 bit per pixel
    ETextureFormat_8BPP = 2            #// 8 bits per pixel
    ETextureFormat_RGB16 = 3           #// 16BPP
    ETextureFormat_RGBA16 = 4          #// 16BPP + 8bit alpha

    TextureFormatToId = {
        Texture.Format1BPP: ETextureFormat_1BPP,
        Texture.Format8BPP: ETextureFormat_8BPP,
        Texture.FormatRGB16: ETextureFormat_RGB16,
        Texture.FormatRGBA16: ETextureFormat_RGBA16,
    }


#
#
class GfxResourceInfo:
    HeaderFormat = "<BIB" # type, size, flags


#
#
class GfxGlyphInfo:
    """
        Glyph info
    """    
    GfxGlyphInfoFormat = "<HhhBBBbb"

    def __init__( s, c, x, y, w, h, advance=0 ):
        s.data = []
        s.c = c        
        s.x = x
        s.y = y
        s.w = w
        s.h = h
        s.offsetX = 0
        s.offsetY = 0
        if not advance:
            s.advance = w + 1


    def endAsset( s, compressed ):
        """
        Build full glyph into struct
        """   
        assert not compressed

        #print(s.c, s.x, s.y, s.w, s.h, s.advance, s.offsetX, s.offsetY )
        infoBytes = struct.pack(s.GfxGlyphInfoFormat, s.c, s.x, s.y, s.w, s.h, s.advance, s.offsetX, s.offsetY )                                  
        s.data = toArray(infoBytes)             


#
#
class AudioInfoAsset:
    """
        Audio info
    """    
    AudioInfoAssetFormat = "<BBBII"

    def __init__( s, assetType, assetFlags, exportSymbolName):
        s.assetType = assetType
        s.assetFlags = assetFlags
        s.exportSymbolName = exportSymbolName
        s.format = 0
        s.channels = 0
        s.isCompressed = 0
        s.uncompressedSize = 0
        s.data = []


    def endAsset( s, compressed ):
        """
        Build audio
        """     
        assert not compressed   # Not supported
        
        infoBytes = struct.pack(s.AudioInfoAssetFormat, s.format, s.channels, s.isCompressed, s.uncompressedSize, len(s.data) )
        size = struct.calcsize(GfxResourceInfo.HeaderFormat) + len(infoBytes) + len(s.data) 
        headerBytes = struct.pack(GfxResourceInfo.HeaderFormat, s.assetType, size, s.assetFlags )  
        s.data = toArray(headerBytes) + toArray(infoBytes) + s.data



#
#
class GfxTextureInfo:
    """
        Texture info
    """
    TextureFormat = "<BHHIII" # format, w, h, palAssetId, dataSize, dataOffset

    def __init__( s, sourceFilename, assetType, assetFlags, exportSymbolName,
                format=0, w=0, h=0):
        s.sourceFilename = sourceFilename
        s.assetType = assetType
        s.assetFlags = assetFlags
        s.exportSymbolName = exportSymbolName

        s.format = format
        s.w = w
        s.h = h        
        s.palAssetId = 0
        s.textureData = []
        s.dataOffset = 0


    def endAsset( s, compressed ):
        """
        Build full texture into struct
        """     
        if compressed:
            s.dataOffset = compressed.appendCompressionData( s.textureData ) # append compression block

        # build TextureInfo
        textureInfoBytes = struct.pack(s.TextureFormat, s.format, s.w, s.h, s.palAssetId, len(s.textureData), s.dataOffset )    

        # build header
        size = struct.calcsize(GfxResourceInfo.HeaderFormat) + len(textureInfoBytes) + len(s.textureData) 
        headerBytes = struct.pack(GfxResourceInfo.HeaderFormat, s.assetType, size, s.assetFlags )                                  
        
        if compressed:
            s.data = toArray(headerBytes) + toArray(textureInfoBytes)             
        else:
            s.data = toArray(headerBytes) + toArray(textureInfoBytes) + s.textureData

        return s


    def __repr__( s ):
        return "GfxTextureInfo(%s,%d)" % (s.sourceFilename, s.assetType) 


    def saveAsPng( s, filename ):
        """
            NOTE: Very limited export, hacked in to export font bmps from vector assets.
        """
        im = Image.new("RGBA", (s.w, s.h))

        pixels = []

        offset = 0
        
        if s.format == ETextureFormat.ETextureFormat_8BPP:
            # Limited support,
            for i in range(s.h):
                for j in range(s.w):

                    # Just dumps raw index to R values
                    # TODO: handle pal assets ( requires linkage import )
                    px = (s.textureData[offset],0,0,255)
                    offset += 1

                    pixels.append(px)
        else:
            raise Exception("saveAsPng doesn't support texture format '%s'" % str(s.format))

        im.putdata(pixels)
        im.save( filename )


#
#
class GfxFontInfo:
    """
        Font info
    """    
    FontInfoFormat = "<IBBH" #assetId, spaceWidth, lineHeight, glyphCnt

    def __init__( s, sourceFilename, assetType, assetFlags, exportSymbolName ):
        # add GfxResourceInfo
        s.sourceFilename = sourceFilename
        s.data = [] # commited (endAsset)        
        s.exportSymbolName = exportSymbolName        
        s.assetType = assetType
        s.assetFlags = assetFlags        
        s.spaceWidth = 8
        s.lineHeight = 8

        s.glyphs = []
        s.assetId = None 
        s.glyphCnt = None
            

    def endAsset( s, compressed ):
        """
        Build full font into struct
        """     
        assert not compressed
    
        # build GfxFontInfo
        fontInfoBytes = struct.pack(s.FontInfoFormat, s.assetId, s.spaceWidth, s.lineHeight, len(s.glyphs) )    

        # add glyph data for glyphs[] records
        concatGlyphData = []
        for i in s.glyphs:
            concatGlyphData += i.data

        # build header
        size = struct.calcsize(GfxResourceInfo.HeaderFormat) + len(fontInfoBytes) + len(concatGlyphData) 
        headerBytes = struct.pack(GfxResourceInfo.HeaderFormat, s.assetType, size, s.assetFlags )                                  

        s.data = toArray(headerBytes) + toArray(fontInfoBytes) + concatGlyphData

    def __repr__( s ):
        return "GfxFontInfo(%s,%d)" % (s.sourceFilename, s.assetType) 
 

#
#
def realToBGR565( c ):    
    r = c[0] * 31
    g = c[1] * 63
    b = c[2] * 31    
    return ( int(b), int(g), int(r) )


def packRGB565( col ):
    assert col[0] <= 0b11111
    assert col[1] <= 0b111111
    assert col[2] <= 0b11111
    col = ((col[0] & 0b11111) << 0) | ((col[1] & 0b111111) << 5) | ((col[2] & 0b11111) << 11)
    assert col <= 0xffff
    return col

def rgbTo565( r,g,b ):
    c = realToBGR565((r/255.0,g/255.0,b/255.0))
    return packRGB565(c)


#
#
def convertTexture16BPP( filename, outputStream, format, exportSymbolName, flipV=False, flipH=False ):
    """
        Convert image to texture
    """
    # open image
    img = Image.open(filename)

    if flipH:
        img = img.transpose(Image.FLIP_LEFT_RIGHT)
    if flipV:
        img = img.transpose(Image.FLIP_TOP_BOTTOM)

    exportSymbolNameTex = None
    if exportSymbolName:
        exportSymbolNameTex = exportSymbolName + "_texture"
    texTextureStream = GfxTextureInfo(filename, assetType=EGfxAssetType.EGfxAssetType_Texture, assetFlags=0, exportSymbolName=exportSymbolNameTex);
    texTextureStream.textureData = [0] * (img.size[0] * img.size[1] * 2) # alloc image chars in single line
    texTextureStream.w = img.size[0]
    texTextureStream.h = img.size[1] 
    texTextureStream.format = ETextureFormat.ETextureFormat_RGB16

    # encode
    index = 0
    for y in range(img.size[1]):
        for x in range(img.size[0]):
            col = img.getpixel((x,y))
            colOut = realToBGR565( (col[0] / 255.0, col[1] / 255.0, col[2] / 255.0) )
            col16 = packRGB565(colOut)    

            # msb first
            texTextureStream.textureData[index+1] = (col16 & 0xff00) >> 8
            texTextureStream.textureData[index+0] = col16 & 0xff
            index += 2
    
    outputStream.addGfxResourceInfoPacker(texTextureStream);    
    

#
#
class SplineInfo:
    """
        spline info
    """
    MeshFormat = "I" # pCnt
    TriFormat = "ffff" # vx,vy,vz,vw

    def __init__( s, sourceFilename, assetType, assetFlags, exportSymbolName,
                format=0, w=0, h=0):
        s.sourceFilename = sourceFilename
        s.assetType = assetType
        s.assetFlags = assetFlags
        s.exportSymbolName = exportSymbolName  

        s.p = []
        

    def endAsset( s, compressed ):
        """
        Build full verts into struct
        """     
        assert not compressed

        # build SplineInfo
        splineInfoBytes = struct.pack(SplineInfo.MeshFormat, len(s.p) )  # add tris cnt  

        # calc total size
        triSize = struct.calcsize(SplineInfo.TriFormat)
        totalSize = struct.calcsize(GfxResourceInfo.HeaderFormat) + len(splineInfoBytes) + (len(s.p) * triSize)
        
        # build header        
        headerBytes = struct.pack(GfxResourceInfo.HeaderFormat, s.assetType, totalSize, s.assetFlags )                                  

        s.data = toArray(headerBytes) + toArray(splineInfoBytes)

        # build tris
        for i in s.p:
            trisBytes = struct.pack(SplineInfo.TriFormat, *i )                                  
            s.data += toArray(trisBytes)

    def __repr__( s ):
        return "SplineInfo(%s,%d)" % (s.sourceFilename, s.assetType) 


#
#
def convertSplineOBJ( filename, outputStream, format, exportSymbolName, depth=0 ):
    """
        Convert spline obj
    """
    logIndent = ""
    for i in range(depth):
        logIndent += "\t"

    vertices = []
    normals = []
    texcoords = []
    faces = []
    lines = []
    MTL = {}
    swapyz = False

    material = None
    for line in open( filename, "r" ):
        if line.startswith('#'): 
            continue

        values = line.split()
        if not values: 
            continue

        if values[0] == 'v':
            v = map(float, values[1:4])
            if swapyz:
                v = v[0], v[2], v[1]

            vals = []
            for i in v:                
                vals.append(i)

            vertices.append(vals)

        elif values[0] == 'vn':
            v = map(float, values[1:4])
            if swapyz:
                v = v[0], v[2], v[1]
            normals.append(v)

        elif values[0] == 'vt':
            v =  map(float, values[1:3])

            vals = []
            for i in v:                
                vals.append(i)

            #vals[1] = 1-vals[1] #

            texcoords.append(vals)

        elif values[0] in ('usemtl', 'usemat'):
            material = values[1]

        elif values[0] == 'mtllib':
            if values[1] in MTL:
                mtl = MTL(values[1])

        elif values[0] == 'f':
            face = []
            texcoordIds = []
            norms = []
            for v in values[1:]:
                w = v.split('/')
                face.append(int(w[0]))
                if len(w) >= 2 and len(w[1]) > 0:                    
                    texcoordIds.append(int(w[1]))                    
                else:
                    texcoordIds.append(0)
                if len(w) >= 3 and len(w[2]) > 0:
                    norms.append(int(w[2]))
                else:
                    norms.append(0)
            faces.append((face, norms, texcoordIds, material))

        elif values[0] == 'l':            

            linePointIds = []
            for v in values[1:]:
                linePointIds.append(int(v))
            
            if len(linePointIds) == 2:
                lines.append(linePointIds)                
        
    splineInfo = SplineInfo(filename, assetType=EGfxAssetType.EGfxAssetType_Spline, assetFlags=0, exportSymbolName=exportSymbolName);

    # build line list
    linePool = [] + lines
    
    while(linePool):
        line = linePool[0]
        del linePool[0]
        #print("startline", line)

        currentLinePoints = []

        while(linePool):
            # inter until no more connections
            connections = []
            for i in linePool:            
                if i[0] in line or i[1] in line:
                    connections.append(i)
            
            if len(connections) > 1:
                raise Exception("line has multiple brances")
            
            if not connections:            
                print("\t end")
                break

            connection = connections[0]
            linePool.remove(connection)
            
            if connection[0] in line: # 0 = connector
                fromId = connection[0] # connector
                toId = connection[1] # other            
            else:
                fromId = connection[1]
                toId = connection[0]    

            if not currentLinePoints:                
                if len(currentLinePoints) == 0: # ack
                    
                    if fromId == line[1]: # 0 1
                        currentLinePoints.append( vertices[ line[0]-1 ])
                        currentLinePoints.append( vertices[ line[1]-1 ])                    
                    else:
                        raise Exception("Unhandled join case") # need to add other combinations, rely on blender ordering 

                currentLinePoints.append( vertices[fromId-1]);
            currentLinePoints.append( vertices[toId-1]);
        
            line = connection

        # loop
        currentLinePoints.append(currentLinePoints[0])
            
        #print(currentLinePoints)
        # add line
        for i in currentLinePoints:
            tri = []
            tri += i + [1]   
            splineInfo.p.append(tri)

    print(logIndent, "\t", "convert spline:", filename, format, exportSymbolName)
    print(logIndent, "\t", "tris:", len(splineInfo.p))

     # add resource and index asset 
    outputStream.addGfxResourceInfoPacker(splineInfo);    




#
#
class BinAsset:
    """
        Simple binary blob assets
    """

    def __init__( s, sourceFilename, assetType, assetFlags, exportSymbolName,
                format=0, w=0, h=0):
        s.sourceFilename = sourceFilename
        s.assetType = assetType
        s.assetFlags = assetFlags
        s.exportSymbolName = exportSymbolName
        s.noheader = False

        s.bytes = []


    def endAsset( s, compressed ):
        """
        build bin header
        """    
        assert not compressed

        if s.noheader:
            # build 
            s.data = []
            for i in s.bytes:            
                s.data.append(i)

        else:            
            # calc total size
            totalSize = struct.calcsize(GfxResourceInfo.HeaderFormat) + len(s.bytes)
            
            # build header        
            headerBytes = struct.pack(GfxResourceInfo.HeaderFormat, s.assetType, totalSize, s.assetFlags )                                  

            s.data = toArray(headerBytes)

            # build 
            for i in s.bytes:            
                s.data.append(i)


#
#
def embedBin( filename, outputStream, format, exportSymbolName, depth=0, customAssetType=0xff, noheader=False, compressed=False ):
    """
        Embed bin stream
    """
    logIndent = ""
    for i in range(depth):
        logIndent += "\t"

    data = open(filename, "rb").read()
    
    asset = BinAsset(filename, assetType=customAssetType, assetFlags=0, exportSymbolName=exportSymbolName);

    if compressed:
        asset.bytes = compress(data)
    else:
        for i in data:
            asset.bytes.append(i)

    asset.noheader = noheader
    
    print(logIndent, "\t", "add bin:", filename, format, exportSymbolName)
    print(logIndent, "\t", "data sz:", len(asset.bytes))
    print(logIndent, "\t", "customAssetType:", customAssetType)
    print(logIndent, "\t", "noheader:", noheader)
    
    # add resource and index asset     
    outputStream.addGfxResourceInfoPacker(asset);    


#
#
class MeshInfoAssetWrapper:
    """
        MeshInfo asset wrapper
    """        
    def __init__( s, assetType, assetFlags, exportSymbolName):
        s.assetType = assetType
        s.assetFlags = assetFlags
        s.exportSymbolName = exportSymbolName
        s.tgxWriter = None
        
    def endAsset( s, compressed ):        
        assert s.tgxWriter
        
        # build header
        s.tgxWriter.buildHeader( headerOffset=0) 

        # calc final size
        size = len(s.tgxWriter.headerBytes) + len(s.tgxWriter.dataBytes)

        # get asset header
        headerBytes = struct.pack(GfxResourceInfo.HeaderFormat, s.assetType, size, s.assetFlags )  

        # build final data
        s.data = toArray(headerBytes) + toArray(s.tgxWriter.headerBytes) + toArray(s.tgxWriter.dataBytes)


#
#
def convertModelOBJ( filename, outputStream, format, exportSymbolName, matInfoJson, depth=0 ):
    """
        Convert model obj

        matInfoJson: Json blob from asset json
        {

        }
    """
    logIndent = ""
    for i in range(depth):
        logIndent += "\t"

    print(logIndent, "Model %s" % (filename )    )

    # Convert to bin using TGX bin format
    tgxWriter = model_converter.convertObj( filename=filename, modelname="model", defaultColLighting=True, outputFilename=None, lang='bin' )
    
    # Create asset to insert
    meshInfo = MeshInfoAssetWrapper(assetType=EGfxAssetType.EGfxAssetType_Mesh, assetFlags=0, exportSymbolName=exportSymbolName);
    meshInfo.tgxWriter = tgxWriter # set writer for asset to build process

    outputStream.addGfxResourceInfoPacker(meshInfo);



#
#
def convertFontBdf( filename, charmap, outputStream, depth=0, exportSymbolName=None, packId=0, exportPng=None, glyphOffsetsX={} ):
    """
        Convert BDF font

        Writes assets:
            GfxTextureInfo
            [
                ..texture data bytes
            ]
            GfxFontInfo
            [
                ..GfxGlyphInfo[]
            ]
    """    
    logIndent = ""
    for i in range(depth):
        logIndent += "\t"

    font = Font(filename)
    print(logIndent, "Font %s, glyphs: %d" % (filename, len(font)) )
    print(logIndent, "\tcharmap:", charmap)

    # count usable glyphs
    glyphs = []
    totalW = 0
    maxH = 0
    for glyph in font.iterglyphs(r=None):
        if charmap and not glyph.chr() in charmap:
            continue
        glyphs.append(glyph)
        bmp = glyph.draw()         
        if bmp.height() > maxH:
            maxH = bmp.height()
        totalW += bmp.width();
    
    if(maxH == None or maxH <= 0):
        return # Nothing to do

    print(logIndent, "\ttotalW:", totalW)
    print(logIndent, "\tmaxH:", maxH)

    # export texture first (dep asset id)
    fontInfoStream = GfxFontInfo(filename, assetType=EGfxAssetType.EGfxAssetType_FontInfo, assetFlags=0, exportSymbolName=exportSymbolName);
    fontInfoStream.lineHeight = font.headers['fbby']
    #print(font.headers)
    print(logIndent, "\tlineHeight:", fontInfoStream.lineHeight)
    
    exportSymbolNameTex = None
    if exportSymbolName:
        exportSymbolNameTex = exportSymbolName + "_texture"
    fontTextureStream = GfxTextureInfo(filename, assetType=EGfxAssetType.EGfxAssetType_Texture, assetFlags=0, exportSymbolName=exportSymbolNameTex);
    fontTextureStream.textureData = [0] * (maxH * totalW) # alloc image chars in single line
    
    lastX = 0
    for glyph in glyphs: 
        bmp = glyph.draw()
        maxW = 0
        for y in range(bmp.height()):
            row = bmp.bindata[y]
            
            for x in range(len(row)):
                v = row[x]           
                index = lastX + x + (y*totalW)      
                fontTextureStream.textureData[ index ] = int(v)
           
            if len(row) > maxW:
                maxW = len(row)

        # debug sanity mark char boundry
        #for i in range(maxH-1):
            #fontTextureStream.textureData[ lastX ] = 1

        # append glyph with texture data offset
        info = GfxGlyphInfo( c=ord(glyph.chr()), x=lastX, y=0, w=bmp.width(), h=bmp.height() )      
   
        info.offsetX = -glyph.origin()[0]
        info.offsetY = 0#glyph.meta['bbyoff']
        info.advance = glyph.meta['bbw'] + 1

        if glyph.chr() in glyphOffsetsX:
            info.offsetX += glyphOffsetsX[glyph.chr()]

        # append glyph
        info.endAsset( compressed=False )
        fontInfoStream.glyphs.append(info)

        # advance texture layout
        lastX += maxW
        
    # Add texture to stream ( need asset id )        
    fontTextureStream.w = totalW
    fontTextureStream.h = maxH 
    fontTextureStream.format = ETextureFormat.ETextureFormat_8BPP
    fontTextureStream.endAsset( compressed=False )

    fontTextureAssetId = outputStream.addGfxResourceInfoPacker(fontTextureStream);
    print(logIndent, "\tfont texture size:", fontTextureStream.w, fontTextureStream.h)    
    
    # commit font info    
    fontInfoStream.assetId = encodeResourceId( packId, fontTextureAssetId)

    # add resource and index asset 
    outputStream.addGfxResourceInfoPacker(fontInfoStream);    

    if exportPng:
        fontTextureStream.saveAsPng(exportPng)


#
#
class GfxFrameInfo:
    """
        Frame info
    """
    GfxFrameInfoFormat = "<HHHHHH" #x, y, w, h
    def __init__( s ):
        s.x = 0
        s.y = 0
        s.w = 0
        s.h = 0
        s.pivotX = 0
        s.pivotY = 0
    
    def toData( s ):        
        s.data = struct.pack(s.GfxFrameInfoFormat, s.x, s.y, s.w, s.h, s.pivotX, s.pivotY)

        return s.data


    def __repr__( s ):
        return "GfxFrameInfo()"
    

#
#
class GfxAnimInfo:
    """
        Anim info
    """    
    GfxAnimInfoFormat = "<IH" #assetId, frameCnt, GfxFrameInfo frames[]

    def __init__( s, sourceFilename, assetType, assetFlags, exportSymbolName ):
        # add GfxResourceInfo
        s.sourceFilename = sourceFilename
        s.data = [] # commited (endAsset)        
        s.exportSymbolName = exportSymbolName        
        s.assetType = assetType
        s.assetFlags = assetFlags        

        s.assetId = None 
        s.frames = []
            

    def endAsset( s, compressed ):
        """
        Build full font into struct
        """    
        
        # build GfxFontInfo
        assert s.assetId != None, "Ensure texture assetId set"
        animInfoBytes = struct.pack(s.GfxAnimInfoFormat, s.assetId, len(s.frames) )    

        # add frames
        concatFrameData = []
        for i in s.frames:            
            concatFrameData += i.toData()

        # build header
        size = struct.calcsize(GfxResourceInfo.HeaderFormat) + len(animInfoBytes) + len(concatFrameData) 
        headerBytes = struct.pack(GfxResourceInfo.HeaderFormat, s.assetType, size, s.assetFlags )                                  

        s.data = toArray(headerBytes) + toArray(animInfoBytes) + concatFrameData

        return s


    def __repr__( s ):
        return "GfxAnimInfo(%s,%d)" % (s.sourceFilename, s.assetType) 
    

#
#
def convertRGBAImageDataTo16BPP( filename, exportSymbolName, imgData, imgSize, alphaClip=0):
    """
        Convert image data to texture
    """    
    exportSymbolNameTex = None
    if exportSymbolName:
        exportSymbolNameTex = exportSymbolName + "_texture"
    texTextureStream = GfxTextureInfo(filename, assetType=EGfxAssetType.EGfxAssetType_Texture, assetFlags=0, exportSymbolName=exportSymbolNameTex);
    texTextureStream.textureData = [0] * (imgSize[0] * imgSize[1] * 2) # alloc image chars in single line
    texTextureStream.w = imgSize[0]
    texTextureStream.h = imgSize[1] 
    texTextureStream.format = ETextureFormat.ETextureFormat_RGB16

    # encode
    index = 0
    for y in range(imgSize[1]):
        for x in range(imgSize[0]):
            col = imgData[  x + (y * imgSize[0]) ]            
            colOut = realToBGR565( (col[0] / 255.0, col[1] / 255.0, col[2] / 255.0) )
            col16 = packRGB565(colOut)    

            # no colkey, ensure always 1 with alpha 
            if( col16 == 0):
                col16 = 1

            # clip alpha to zero
            if( col[3] <= alphaClip):
                col16 = 0
            
            # msb first
            texTextureStream.textureData[index+1] = (col16 & 0xff00) >> 8
            texTextureStream.textureData[index+0] = col16 & 0xff
            index += 2

    return texTextureStream
    

#
#
def convertIndexmageDataTo8BPP( filename, exportSymbolName, imgData, imgSize, alphaClip=0):
    """
        Convert image data to texture
    """    
    exportSymbolNameTex = None
    if exportSymbolName:
        exportSymbolNameTex = exportSymbolName + "_texture"
    texTextureStream = GfxTextureInfo(filename, assetType=EGfxAssetType.EGfxAssetType_Texture, assetFlags=0, exportSymbolName=exportSymbolNameTex);
    texTextureStream.textureData = [0] * (imgSize[0] * imgSize[1] * 2) # alloc image chars in single line
    texTextureStream.w = imgSize[0]
    texTextureStream.h = imgSize[1] 
    texTextureStream.format = ETextureFormat.ETextureFormat_8BPP

    # encode
    index = 0
    for y in range(imgSize[1]):
        for x in range(imgSize[0]):
            colI = imgData[  x + (y * imgSize[0]) ]            
            
            # msb first
            texTextureStream.textureData[index] = colI           
            index += 1

    return texTextureStream
    


#
#
def saveRGBAImageToFile( imgData, imgSize, filename=None, show=None ):
    image = Image.new(mode="RGBA", size=imgSize)
    w,h = imgSize
    for y in range(h):
        for x in range(w):
            srcId = ( x + (y * w) ) 
            image.putpixel( (x,y), imgData[srcId])

    if filename:
        image.save(filename)

    if show:
        image.show(title=show)

#
#
def convertAseCellToImageRGBA( frameSize, cel ):
    """
        Convert ase cell and comp into full frame ( ase has cells offset in file, flatten / size to document size for simplicity )

        returns w, h, [ (r,g,b,a) ... ]
    """
    pixels, w, h, offsetX, offsetY = cel.data['data'], cel.data['width'], cel.data['height'], cel.x_pos, cel.y_pos

    # alloc frame pixels
    img = [(0,0,0,0)] * (frameSize[0]*frameSize[1])

    # comp cel into img
    for y in range(h):
        for x in range(w):
            srcId = ( x + (y * w) ) * 4
            colRGBA = pixels[ srcId + 0 ], pixels[ srcId + 1 ], pixels[ srcId + 2 ], pixels[ srcId + 3 ]
            
            dstId = ( (x + offsetX) + ((y + offsetY) * frameSize[0]) )

            if dstId >= 0 and dstId < len(img):
                img[ dstId ] = colRGBA
            
    return img


#
#
def convertAseCellToImageIndexed( frameSize, cel ):
    """
        Convert ase cell and comp into full frame ( ase has cells offset in file, flatten / size to document size for simplicity )

        returns w, h, [ index ... ]
    """
    pixels, w, h, offsetX, offsetY = cel.data['data'], cel.data['width'], cel.data['height'], cel.x_pos, cel.y_pos

    # alloc frame pixels
    img = [0] * (frameSize[0]*frameSize[1])

    # comp cel into img
    for y in range(h):
        for x in range(w):
            srcId = ( x + (y * w) ) * 1
            colI = pixels[ srcId + 0 ]
            
            dstId = ( (x + offsetX) + ((y + offsetY) * frameSize[0]) )

            if dstId >= 0 and dstId < len(img):
                img[ dstId ] = colI
            
    return img



#
#
def packCellVerticalToImageRGBA( dstImage, dstImageSize, frameSize, packOffset, img ):
    """
        Vertical pack images ( rgba )
    """
    for y in range(frameSize[1]):
        for x in range(frameSize[0]):
            srcId = ( x + (y * frameSize[0]) )
            colRGBA = img[srcId]

            dstId = ( (x + packOffset[0]) + ( (y+packOffset[1]) * dstImageSize[0] ) )

            if dstId >= 0 and dstId < len(dstImage):
                dstImage[ dstId ] = colRGBA


#
#
def packCellVerticalToImageIndexed( dstImage, dstImageSize, frameSize, packOffset, img ):
    """    
        Vertical pack images (index color)
    """
    for y in range(frameSize[1]):
        for x in range(frameSize[0]):
            srcId = ( x + (y * frameSize[0]) )
            colI = img[srcId]

            dstId = ( (x + packOffset[0]) + ( (y+packOffset[1]) * dstImageSize[0] ) )

            if dstId >= 0 and dstId < len(dstImage):
                dstImage[ dstId ] = colI


#
#
def readAsePalette( filename, mainLayerName, outputStream, format, exportSymbolName, packId, pivotLayerName="pivot", exportPalette=True ):
    print("\t\treadAsePalette filename:", filename, mainLayerName, exportPalette)
    with open(filename, 'rb') as f:
        ase = AsepriteFile(f.read())        
        
        isIndexed = False
        palChunk = None
        palAsset = None
        palAssetId = None
        if ase.header.color_depth == 8:
            isIndexed = True
            # find pal
            for i in range(len(ase.frames)):
                frame = ase.frames[i]            
                for j in range(len(frame.chunks)):
                    chunk = frame.chunks[j]            
                    if isinstance(chunk, OldPaleteChunk_0x0004):
                        palChunk = chunk
                            
            assert palChunk

            colors = palChunk.packets[0]['colors'] 

            # create pal asset
            palAsset = GfxTextureInfo(filename, assetType=EGfxAssetType.EGfxAssetType_Texture, assetFlags=0, exportSymbolName=exportSymbolName + "_pal");             
            palAsset.textureData = [0] * ( len(colors) * 2 ) # 16bit
            palAsset.w = len(colors)
            palAsset.h = 1
            palAsset.format = ETextureFormat.ETextureFormat_RGB16
                       
            palAsset.pal = []
            index = 0
            for i in range(len(colors)):
                cRGB = colors[i] 

                colOut = realToBGR565( (cRGB[0] / 255.0, cRGB[1] / 255.0, cRGB[2] / 255.0) )
                col16 = packRGB565(colOut)    

                # msb first
                palAsset.textureData[index+1] = (col16 & 0xff00) >> 8
                palAsset.textureData[index+0] = col16 & 0xff

                index += 2
            
            outputStream.addGfxResourceInfoPacker( palAsset );                



#
#
def readAseAnim( filename, mainLayerName, outputStream, format, exportSymbolName, packId, pivotLayerName="pivot", exportPalette=True ):
    print("\t\treadAseAnim filename:", filename, mainLayerName, exportPalette)
    animInfos = []
    with open(filename, 'rb') as f:
        ase = AsepriteFile(f.read())        
        
        isIndexed = False
        palChunk = None
        palAsset = None
        palAssetId = None
        if ase.header.color_depth == 8:
            isIndexed = True
            palAssetId = 0 # null default

            if exportPalette:                
                # find pal
                for i in range(len(ase.frames)):
                    frame = ase.frames[i]            
                    for j in range(len(frame.chunks)):
                        chunk = frame.chunks[j]            
                        if isinstance(chunk, OldPaleteChunk_0x0004):
                            palChunk = chunk
                            colors = palChunk.packets[0]['colors'] 
                        elif isinstance(chunk, OldPaleteChunk_0x0011):
                            colors = palChunk.packets[0]['colors'] 
                        elif isinstance(chunk, PaletteChunk):
                            palChunk = chunk
                            colors = []
                            for cDict in palChunk.colors:
                                colors.append( (cDict['red'], cDict['green'], cDict['blue']) )
                            # convert to tuple, zero consistency 
                            print(colors)                            

                                
                assert palChunk

                # create pal asset
                palAsset = GfxTextureInfo(filename, assetType=EGfxAssetType.EGfxAssetType_Texture, assetFlags=0, exportSymbolName=exportSymbolName + "_pal");             
                palAsset.textureData = [0] * ( len(colors) * 2 ) # 16bit
                palAsset.w = len(colors)
                palAsset.h = 1
                palAsset.format = ETextureFormat.ETextureFormat_RGB16
                        
                palAsset.pal = []
                index = 0
                for i in range(len(colors)):
                    cRGB = colors[i] 

                    colOut = realToBGR565( (cRGB[0] / 255.0, cRGB[1] / 255.0, cRGB[2] / 255.0) )
                    col16 = packRGB565(colOut)    

                    # msb first
                    palAsset.textureData[index+1] = (col16 & 0xff00) >> 8
                    palAsset.textureData[index+0] = col16 & 0xff

                    index += 2

                palAssetId = outputStream.addGfxResourceInfoPacker( palAsset );                


        # extract anims from tags
        anims = []
        for i in range(len(ase.frames)):
            frame = ase.frames[i]            
            for j in range(len(frame.chunks)):
                chunk = frame.chunks[j]                
                if isinstance(chunk, FrameTagsChunk):
                    for tag in chunk.tags:
                        anims.append( {'name': tag['name'], 'from': tag['from'], 'to': tag['to']}  )

        # iter over anims
        for anim in anims:
            animName = anim['name']
            print("\t\tanim", animName)

            # create stacked output image
            frameCnt = (anim['to']-anim['from']) + 1
            frameSize = (ase.header.width, ase.header.height)
            stackedSize = (ase.header.width, ( ase.header.height * frameCnt ) )
            stackedImg = [(0,0,0,0)] * ( stackedSize[0] * stackedSize[1] )

            frameInfos = []

            for frameId in range(anim['from'], anim['to']+1):
                localFrameIndex = frameId - anim['from']
                print("\t\t\tframe", frameId, localFrameIndex)

                # get cells
                frameChunks = ase.frames[frameId].chunks 
                cells = [None] * len(ase.layers)
                for chunk in frameChunks:
                    if isinstance(chunk, CelChunk):
                        cells[chunk.layer_index] = chunk
                        
                mainLayer = None
                pivot = (0,0)
                for i in range(len(ase.layer_tree)):
                    layer = ase.layers[i]
                    cel = cells[i]

                    if layer.name == mainLayerName:      
                        if isIndexed:                  
                            mainLayer = convertAseCellToImageIndexed( frameSize=frameSize, cel=cel )                        
                        else:
                            mainLayer = convertAseCellToImageRGBA( frameSize=frameSize, cel=cel )                        
                    elif layer.name == pivotLayerName and cel:
                        # scan pivot layer ( first instance found )
                        pivot = (cel.x_pos, cel.y_pos)
                
                if mainLayer:
                    assert len(mainLayer) == (frameSize[0]*frameSize[1])

                    # Debug dump cells
                    #if mainLayerName == "xxxx":                        
                        #saveRGBAImageToFile( mainLayer, frameSize, "temp/curr%d.png" % localFrameIndex) # debug

                    if isIndexed:
                        packCellVerticalToImageIndexed( stackedImg, stackedSize, frameSize, (0, localFrameIndex * frameSize[1]), mainLayer )
                    else:
                        packCellVerticalToImageRGBA( stackedImg, stackedSize, frameSize, (0, localFrameIndex * frameSize[1]), mainLayer )

                else:
                    foundFayerNames = []
                    for i in range(len(ase.layer_tree)):
                        layer = ase.layers[i]
                        foundFayerNames.append(layer.name)

                    raise Exception("Missing mainLayer named '%s', no frame exported, found: %s" % ( str(mainLayerName), str(foundFayerNames)) )

                frameInfo = GfxFrameInfo()
                frameInfo.x = 0
                frameInfo.y =  localFrameIndex * frameSize[1]
                frameInfo.w = frameSize[0]
                frameInfo.h = frameSize[1]
                frameInfo.pivotX = pivot[0]
                frameInfo.pivotY = pivot[1]
                frameInfos.append( frameInfo )
            
            exportSymbolNameTex = exportSymbolName + "_" + animName + "_Tex"
            if isIndexed:
                stackedTexture = convertIndexmageDataTo8BPP( filename, exportSymbolNameTex, stackedImg, stackedSize )
            else:
                stackedTexture = convertRGBAImageDataTo16BPP( filename, exportSymbolNameTex, stackedImg, stackedSize )

            # debug dump stacks
            #if "xxxx" in exportSymbolName:      
                #saveRGBAImageToFile( stackedImg, stackedSize, "temp/%s.png" % (exportSymbolName + "_" + animName) )                

            # add texture            
            if isIndexed and exportPalette:
                stackedTexture.palAssetId = encodeResourceId( packId, palAssetId ) 
                print("stackedTexture.palAssetId ", stackedTexture.palAssetId )

            textureAssetId = outputStream.addGfxResourceInfoPacker( stackedTexture );
            
            # add anim info & link texture id
            animInfo = GfxAnimInfo(filename, assetType=EGfxAssetType.EGfxAssetType_AnimInfo, assetFlags=0, exportSymbolName=exportSymbolName + "_" + animName);             
            animInfo.assetId = encodeResourceId( packId, textureAssetId)
            animInfo.frames = frameInfos
            outputStream.addGfxResourceInfoPacker( animInfo );    

            animInfos.append( animInfo )
    return animInfos
        

#
#
def convertWavToRawPCM( sourceWav, outputStream, exportSymbolName, depth=0, rate=11025, compressed=False):
    """
        Convert wav to picocom raw pcm.
    """
    logIndent = ""
    for i in range(depth):
        logIndent += "\t"

    with tempfile.NamedTemporaryFile() as tmp:
        tmp.close()
        srcWavFile = wave.open(sourceWav, 'r')
        channelCnt =  srcWavFile.getnchannels()

        tmpWav = tmp.name + ".wav"
        if os.path.exists(tmpWav):
            os.unlink(tmpWav)
        output_str = f"ffmpeg -i {sourceWav} -ac {channelCnt} -ar %d {tmpWav}" % rate
        os.system(output_str)
        print(output_str)

        # read tmp wav
        wavefile = wave.open(tmpWav, 'r')
        print("channels:", wavefile.getnchannels())

        # write raw frames
        asset = AudioInfoAsset(assetType=EGfxAssetType.EGfxAssetType_PCM, assetFlags=0, exportSymbolName=exportSymbolName);
        if channelCnt == 1:
            length = wavefile.getnframes()
            for i in range(0, length):
                wavedata = wavefile.readframes(1)
                for i in wavedata:
                    asset.data.append(i)

        elif channelCnt == 2:
            length = wavefile.getnframes()
            for i in range(0, length):
                wavedata = wavefile.readframes(1)
                for i in wavedata:
                    asset.data.append(i)

        print(logIndent, "\t", "add pcm:", channelCnt, format, exportSymbolName)
        print(logIndent, "\t", "data sz:", len(asset.data))
        
        ## add resource and index asset 
        asset.format = 0 # PCM
        asset.channels = channelCnt

        if compressed:
            srcAssetData = asset.data 
            asset.format = 0 # PCMCompressed
            asset.data = compressList( srcAssetData )
            asset.isCompressed = 1
            asset.uncompressedSize = len( srcAssetData )
            if len(asset.data ) > 25000:
                print("\t\t[OPTIM DEBUG]",sourceWav, "compressedSz:", len(asset.data ))

        outputStream.addGfxResourceInfoPacker(asset);    


def convertFontASE( filename, outputStream, glyphs, depth, exportSymbolName, packId, 
                   format, mainLayerName, exportPalette, lineHeight, gridW, defaultAdvance):
    """
        Convert ase layer horizontal grid font to picocom
    """
    logIndent = ""
    for i in range(depth):
        logIndent += "\t"

    # do normal ase embed
    animInfos = readAseAnim( filename=filename, mainLayerName=mainLayerName, 
                outputStream=outputStream, format=format, exportSymbolName=exportSymbolName + "_bmp", packId=packId, exportPalette=exportPalette )

    if len(animInfos) < 1:
        raise Exception("Failed to export '%s' font ase, no valid layers or frames found, ensure layer and frame tags added" % filename)
    # export texture first (dep asset id)
    fontInfoStream = GfxFontInfo(filename, assetType=EGfxAssetType.EGfxAssetType_FontInfo, assetFlags=0, exportSymbolName=exportSymbolName);
    fontInfoStream.lineHeight = lineHeight
    # commit font info    
    fontInfoStream.assetId = encodeResourceId( packId, animInfos[0].assetId)
    
    print(logIndent, "\tlineHeight:", fontInfoStream.lineHeight)

    lastX = 0
    for i in glyphs:
        c = i['c']

        advance = defaultAdvance
        if 'a' in i:
            advance = i['a']

        info = GfxGlyphInfo( c=ord(c), x=lastX, y=0, w=gridW, h=gridW )      
   
        info.offsetX = 0
        info.offsetY = 0
        info.advance = advance

        # append glyph
        info.endAsset( compressed=False )
        fontInfoStream.glyphs.append(info)        

        lastX += gridW

    outputStream.addGfxResourceInfoPacker(fontInfoStream);   



#
#
class Stats:
    def __init__(s):
        s.packStats = []

    def printReport( s ):
        print("Mem report")
        totalMem = 0
        for i in s.packStats:
            totalMem += i['totalMem']
            print("\t", i['name'], i['totalMem'])
        
        print("\ttotalMem: %dK (%d bytes)" % (totalMem/1024, totalMem) )

    def addPackMemStats( s, name, totalMem ):
        s.packStats.append( {'name': name, 'totalMem': totalMem })



def packAssets( assetFile, outputDir, packIdMap, depth=0, stats=Stats() ):
    """
        Pack assets into stream list
    """    
    assetInfo = json.loads( open(assetFile, 'r').read() )
    assetBaseDir = os.path.dirname(assetFile)    
    logIndent = ""
    for i in range(depth):
        logIndent += "\t"
    
    print(logIndent, "packAssets", assetFile, outputDir)    

    # auto create
    if not os.path.exists(outputDir):
        os.makedirs(outputDir)

    # process includes
    if 'includes' in assetInfo:
        for i in assetInfo['includes']:
            if 'filename' in i:
                filename = assetBaseDir + "/" + i['filename']
                packAssets( filename, outputDir,  packIdMap, depth=depth+1, stats=stats )

    # get fields
    assets = []
    if 'assets' in assetInfo:
        assets = assetInfo['assets']

    outputType = "inline"
    if 'outputType' in assetInfo:
        outputType = assetInfo['outputType']

    if not assets:
        return
    
    # default output name
    outputFilenames = []
    if 'outputFilenames' in assetInfo:
        outputFilenames = assetInfo['outputFilenames']
        assert type(outputFilenames) == type([])
    else:
        if 'outputFilename' in assetInfo:
            outputFilename = assetInfo['outputFilename']
            outputFilenames = [outputFilename]
        else:
            outputFilename = os.path.basename(assetFile)
            outputFilename = os.path.splitext(outputFilename)[0]
            outputFilename += {"inline":".inl", "file": ".dat"}[outputType]
            outputFilenames = [outputFilename]

    assetPackSymbol = os.path.basename(assetFile)
    if 'symbol' in assetInfo:
        assetPackSymbol = assetInfo['symbol']

    assetPackId = 0
    if 'id' in assetInfo:
        assetPackId = assetInfo['id']        

    if assetPackId in packIdMap:
        raise Exception("Asset pack id %s already used in pack %s" % (str(assetPackId), packIdMap[assetPackId]['name']))
    packIdMap[assetPackId] = {'name': assetFile}

    print(logIndent, "\toutputType:", outputType)
    print(logIndent, "\toutputFilenames:", outputFilenames)
    print(logIndent, "\tsymbol:", assetPackSymbol)
    print(logIndent, "\tid:", assetPackId)

    outputStream = ResourcePackStream(assetPackId, assetPackSymbol)

    # WIP
    # Problem: cant just compress meta data, app needs access so data and meta needs separating, quite a bit of work
    if 'compressed' in assetInfo:
        outputStream.compressed = assetInfo['compressed']

    for i in assets:
        assetType = None
        if 'type' in i:
            assetType = i['type']
        
        assetFilename = None
        if 'file' in i:
            assetFilename = i['file']

        exportSymbolName = None
        if 'id' in i:
            exportSymbolName = i['id']


        # handle asset type
        if assetType == 'font':

            exportPng = None
            if 'exportPng' in i:
                exportPng = outputDir + '/' + i['exportPng']

            glyphOffsetsX = {}
            if 'glyphOffsetsX' in i:
                glyphOffsetsX = i['glyphOffsetsX']

            # get charmap
            charmap = None
            if 'charmap' in i:
                charmap = i['charmap']

            # ensure file
            if not assetFilename:
                print("missing 'file' in font asset: ", i)
                continue

            # detect format
            assetExt = os.path.splitext(assetFilename)[1].lower()
            if assetExt == '.bdf':
                convertFontBdf( filename=assetBaseDir+"/"+assetFilename, outputStream=outputStream, charmap=charmap, 
                               depth=depth+1, exportSymbolName=exportSymbolName, packId=assetPackId, exportPng=exportPng, glyphOffsetsX=glyphOffsetsX )
            else:
                print("Unknown font asset type '%s': " % (str(assetExt)), i)
                continue       

        elif assetType == 'ase_bitmap_font':

            format = ""
            if 'format' in i:
                format = i['format']
            
            mainLayerName = "main"
            if 'mainLayerName' in i:
                mainLayerName = i['mainLayerName']

            exportPalette = True
            if 'exportPalette' in i:
                exportPalette = i['exportPalette']

            # get charmap
            glyphs = []
            if 'glyphs' in i:
                glyphs = i['glyphs']

            lineHeight = 10
            if 'lineHeight' in i:
                lineHeight = i['lineHeight']

            gridW = 10
            if 'gridW' in i:
                gridW = i['gridW']

            defaultAdvance = 10
            if 'defaultAdvance' in i:
                defaultAdvance = i['defaultAdvance']

            # ensure file
            if not assetFilename:
                print("missing 'file' in font asset: ", i)
                continue

            # detect format
            assetExt = os.path.splitext(assetFilename)[1].lower()
            if assetExt == '.ase':
                convertFontASE( filename=assetBaseDir+"/"+assetFilename, outputStream=outputStream, glyphs=glyphs, 
                               depth=depth+1, exportSymbolName=exportSymbolName, packId=assetPackId, format=format, 
                               mainLayerName=mainLayerName, exportPalette=exportPalette,
                               lineHeight=lineHeight, gridW=gridW, defaultAdvance=defaultAdvance)
            else:
                print("Unknown font asset type '%s': " % (str(assetExt)), i)
                continue       

        elif assetType == 'texture':         
            
            format = ""
            if 'format' in i:
                format = i['format']
            
            flipV = False
            if 'flipV' in i:
                flipV = i['flipV']

            flipH = False
            if 'flipH' in i:
                flipH = i['flipH']

            if format == "16BPP":
                convertTexture16BPP( filename=assetBaseDir+"/"+assetFilename, outputStream=outputStream, format=format, exportSymbolName=exportSymbolName, flipV=flipV, flipH=flipH )

            else:
                print("Unknown texture format '%s', " % str(format), i)
                continue

        elif assetType == 'model':         
            
            format = ""
            matInfoJson = {}
            if 'format' in i:
                format = i['format']
            
            if 'mat' in i:
                matInfoJson = i['mat']
            
            if format == "obj":
                convertModelOBJ( filename=assetBaseDir+"/"+assetFilename, outputStream=outputStream, format=format, exportSymbolName=exportSymbolName, matInfoJson=matInfoJson, depth=depth )

            else:
                print("Unknown model format '%s', " % str(format), i)
                continue

        elif assetType == 'spline':         
            
            format = ""
            if 'format' in i:
                format = i['format']
            
            if format == "obj":
                convertSplineOBJ( filename=assetBaseDir+"/"+assetFilename, outputStream=outputStream, format=format, exportSymbolName=exportSymbolName, depth=depth )

            else:
                print("Unknown spline format '%s', " % str(format), i)
                continue

        elif assetType == 'embed':         
            
            format = ""
            if 'format' in i:
                format = i['format']
            
            customAssetType = 0xff
            if 'customAssetType' in i:
                customAssetType = int(i['customAssetType'])

            noheader = False
            if 'noheader' in i:
                noheader = i['noheader']

            compressed = False
            if 'compressed' in i:
                compressed = i['compressed']

            if format == "bin":
                embedBin( filename=assetBaseDir+"/"+assetFilename, outputStream=outputStream, format=format, exportSymbolName=exportSymbolName, depth=depth, customAssetType=customAssetType, noheader=noheader, compressed=compressed )

            else:
                print("Unknown embed format '%s', " % str(format), i)
                continue

        elif assetType == 'ase_anim_texture':     
                        
            format = ""
            if 'format' in i:
                format = i['format']
            
            mainLayerName = "main"
            if 'mainLayerName' in i:
                mainLayerName = i['mainLayerName']

            exportPalette = True
            if 'exportPalette' in i:
                exportPalette = i['exportPalette']

            readAseAnim( filename=assetBaseDir+"/"+assetFilename, mainLayerName=mainLayerName, outputStream=outputStream, format=format, exportSymbolName=exportSymbolName, packId=assetPackId, exportPalette=exportPalette )
        
        elif assetType == 'ase_anim_palette':     
                        
            format = ""
            if 'format' in i:
                format = i['format']
            
            mainLayerName = "main"
            if 'mainLayerName' in i:
                mainLayerName = i['mainLayerName']

            exportPalette = True
            if 'exportPalette' in i:
                exportPalette = i['exportPalette']

            readAsePalette( filename=assetBaseDir+"/"+assetFilename, mainLayerName=mainLayerName, outputStream=outputStream, format=format, exportSymbolName=exportSymbolName, packId=assetPackId, exportPalette=exportPalette )
        
        elif assetType == 'wav':     

            noheader = False
            if 'noheader' in i:
                noheader = i['noheader']

            format = "pcm"
            if 'format' in i:
                format = i['format']

            rate = 11025
            if 'rate' in i:
                rate = i['rate']    

            compressed = False            
            if 'compressed' in i:
                compressed = i['compressed']    

            if format == 'pcm':
                
                convertWavToRawPCM( sourceWav=assetBaseDir+"/"+assetFilename, rate=rate, outputStream=outputStream, exportSymbolName=exportSymbolName, depth=depth, compressed=compressed)

            else:
                raise Exception("Unsupported format, for ogg just embed as raw")
            

        else:
            print("Unknown asset type '%s', " % str(assetType), i)
            continue
    
    for i in outputFilenames:

        stats.addPackMemStats( assetFile, totalMem=len(outputStream.data) )

        if i.endswith('.inl'):        
            outputStream.writeInlineHeader(outputDir + "/" + i)
        elif i.endswith('.dat') or i.endswith('.bin'):
            outputStream.writeBin(outputDir + "/" + i)
        else:
            raise Exception("Unknown output type for ext ", i)

    stats.addPackMemStats( "\tcompressedSection", totalMem=len(outputStream.compressedData) ) # NOTE: name tabbed for printout, really do need a better asset reporting!
    

def main():    
    parser = OptionParser()
    parser.add_option("-f", "--filename",
                      help="Asset json descriptor")

    parser.add_option("-o", "--outputdir",
                      help="Output directory", default="")

    parser.add_option("-a", "--assetCacheDir",
                      help="Source asset dir for hash change detection", default="")
    parser.add_option("-d", "--cacheFilename",
                      help="", default="cache.state")    
    
    (options, args) = parser.parse_args()
    assert options.filename

    writeHashFilename = None
    if options.assetCacheDir:
        writeHashFilename = options.cacheFilename
        hash = hashDir(options.assetCacheDir)
        print("hash:", hash)

        if os.path.exists(writeHashFilename):
            if open(writeHashFilename,'r').read() == hash:
                print("No change")
                return
    
    packIdMap = {}
    stats = Stats()
    packAssets( assetFile=options.filename, outputDir=options.outputdir, packIdMap=packIdMap, stats=stats )

    stats.printReport()

    if writeHashFilename:
        print("write hash", writeHashFilename, hash)
        open(writeHashFilename,'w').write( hash )




if __name__ == '__main__':
    main()
