using System.Collections.Generic;

namespace TiffPlusPlus.Core
{
    internal static class TagNames
    {
        private static readonly Dictionary<ushort, string> Names = new Dictionary<ushort, string>
        {
            [254] = "NewSubfileType", [256] = "ImageWidth", [257] = "ImageLength", [258] = "BitsPerSample",
            [259] = "Compression", [262] = "PhotometricInterpretation", [266] = "FillOrder",
            [269] = "DocumentName", [270] = "ImageDescription", [271] = "Make", [272] = "Model",
            [273] = "StripOffsets", [274] = "Orientation", [277] = "SamplesPerPixel", [278] = "RowsPerStrip",
            [279] = "StripByteCounts", [280] = "MinSampleValue", [281] = "MaxSampleValue",
            [282] = "XResolution", [283] = "YResolution", [284] = "PlanarConfiguration",
            [296] = "ResolutionUnit", [305] = "Software", [306] = "DateTime", [315] = "Artist",
            [316] = "HostComputer", [317] = "Predictor", [320] = "ColorMap", [322] = "TileWidth",
            [323] = "TileLength", [324] = "TileOffsets", [325] = "TileByteCounts", [338] = "ExtraSamples",
            [339] = "SampleFormat", [340] = "SMinSampleValue", [341] = "SMaxSampleValue",
            [700] = "XMP", [33432] = "Copyright", [33723] = "IPTC", [34377] = "Photoshop",
            [34665] = "ExifIFD", [34675] = "ICCProfile",
        };

        public static string Get(ushort id) =>
            Names.TryGetValue(id, out var name) ? name : (id >= 32768 ? "Private" : "Unknown");
    }
}
