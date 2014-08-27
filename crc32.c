/*
    This file is part of VK/KittenPHP-DB-Engine Library.

    VK/KittenPHP-DB-Engine Library is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    VK/KittenPHP-DB-Engine Library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with VK/KittenPHP-DB-Engine Library.  If not, see <http://www.gnu.org/licenses/>.

    Copyright 2009-2012 Vkontakte Ltd
              2009-2012 Nikolai Durov
              2009-2012 Andrei Lopatin
                   2012 Anton Maydell
*/

#include <stdlib.h>
#include <math.h>
#include <assert.h>

#include "crc32.h"

unsigned int crc32_table[256] =
{
  0x00000000, 0x77073096, 0xee0e612c, 0x990951ba,
  0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
  0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
  0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
  0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de,
  0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
  0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec,
  0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
  0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
  0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
  0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940,
  0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
  0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116,
  0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
  0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
  0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
  0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a,
  0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
  0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818,
  0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
  0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
  0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
  0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c,
  0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
  0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2,
  0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
  0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
  0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
  0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086,
  0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
  0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4,
  0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
  0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
  0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
  0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8,
  0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
  0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe,
  0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
  0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
  0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
  0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252,
  0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
  0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60,
  0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
  0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
  0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
  0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04,
  0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
  0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a,
  0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
  0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
  0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
  0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e,
  0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
  0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c,
  0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
  0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
  0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
  0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0,
  0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
  0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6,
  0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
  0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
  0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

unsigned int crc32_table2[256] = 
{
  0x00000000, 0x191b3141, 0x32366282, 0x2b2d53c3,
  0x646cc504, 0x7d77f445, 0x565aa786, 0x4f4196c7,
  0xc8d98a08, 0xd1c2bb49, 0xfaefe88a, 0xe3f4d9cb,
  0xacb54f0c, 0xb5ae7e4d, 0x9e832d8e, 0x87981ccf,
  0x4ac21251, 0x53d92310, 0x78f470d3, 0x61ef4192,
  0x2eaed755, 0x37b5e614, 0x1c98b5d7, 0x05838496,
  0x821b9859, 0x9b00a918, 0xb02dfadb, 0xa936cb9a,
  0xe6775d5d, 0xff6c6c1c, 0xd4413fdf, 0xcd5a0e9e,
  0x958424a2, 0x8c9f15e3, 0xa7b24620, 0xbea97761,
  0xf1e8e1a6, 0xe8f3d0e7, 0xc3de8324, 0xdac5b265,
  0x5d5daeaa, 0x44469feb, 0x6f6bcc28, 0x7670fd69,
  0x39316bae, 0x202a5aef, 0x0b07092c, 0x121c386d,
  0xdf4636f3, 0xc65d07b2, 0xed705471, 0xf46b6530,
  0xbb2af3f7, 0xa231c2b6, 0x891c9175, 0x9007a034,
  0x179fbcfb, 0x0e848dba, 0x25a9de79, 0x3cb2ef38,
  0x73f379ff, 0x6ae848be, 0x41c51b7d, 0x58de2a3c,
  0xf0794f05, 0xe9627e44, 0xc24f2d87, 0xdb541cc6,
  0x94158a01, 0x8d0ebb40, 0xa623e883, 0xbf38d9c2,
  0x38a0c50d, 0x21bbf44c, 0x0a96a78f, 0x138d96ce,
  0x5ccc0009, 0x45d73148, 0x6efa628b, 0x77e153ca,
  0xbabb5d54, 0xa3a06c15, 0x888d3fd6, 0x91960e97,
  0xded79850, 0xc7cca911, 0xece1fad2, 0xf5facb93,
  0x7262d75c, 0x6b79e61d, 0x4054b5de, 0x594f849f,
  0x160e1258, 0x0f152319, 0x243870da, 0x3d23419b,
  0x65fd6ba7, 0x7ce65ae6, 0x57cb0925, 0x4ed03864,
  0x0191aea3, 0x188a9fe2, 0x33a7cc21, 0x2abcfd60,
  0xad24e1af, 0xb43fd0ee, 0x9f12832d, 0x8609b26c,
  0xc94824ab, 0xd05315ea, 0xfb7e4629, 0xe2657768,
  0x2f3f79f6, 0x362448b7, 0x1d091b74, 0x04122a35,
  0x4b53bcf2, 0x52488db3, 0x7965de70, 0x607eef31,
  0xe7e6f3fe, 0xfefdc2bf, 0xd5d0917c, 0xcccba03d,
  0x838a36fa, 0x9a9107bb, 0xb1bc5478, 0xa8a76539,
  0x3b83984b, 0x2298a90a, 0x09b5fac9, 0x10aecb88,
  0x5fef5d4f, 0x46f46c0e, 0x6dd93fcd, 0x74c20e8c,
  0xf35a1243, 0xea412302, 0xc16c70c1, 0xd8774180,
  0x9736d747, 0x8e2de606, 0xa500b5c5, 0xbc1b8484,
  0x71418a1a, 0x685abb5b, 0x4377e898, 0x5a6cd9d9,
  0x152d4f1e, 0x0c367e5f, 0x271b2d9c, 0x3e001cdd,
  0xb9980012, 0xa0833153, 0x8bae6290, 0x92b553d1,
  0xddf4c516, 0xc4eff457, 0xefc2a794, 0xf6d996d5,
  0xae07bce9, 0xb71c8da8, 0x9c31de6b, 0x852aef2a,
  0xca6b79ed, 0xd37048ac, 0xf85d1b6f, 0xe1462a2e,
  0x66de36e1, 0x7fc507a0, 0x54e85463, 0x4df36522,
  0x02b2f3e5, 0x1ba9c2a4, 0x30849167, 0x299fa026,
  0xe4c5aeb8, 0xfdde9ff9, 0xd6f3cc3a, 0xcfe8fd7b,
  0x80a96bbc, 0x99b25afd, 0xb29f093e, 0xab84387f,
  0x2c1c24b0, 0x350715f1, 0x1e2a4632, 0x07317773,
  0x4870e1b4, 0x516bd0f5, 0x7a468336, 0x635db277,
  0xcbfad74e, 0xd2e1e60f, 0xf9ccb5cc, 0xe0d7848d,
  0xaf96124a, 0xb68d230b, 0x9da070c8, 0x84bb4189,
  0x03235d46, 0x1a386c07, 0x31153fc4, 0x280e0e85,
  0x674f9842, 0x7e54a903, 0x5579fac0, 0x4c62cb81,
  0x8138c51f, 0x9823f45e, 0xb30ea79d, 0xaa1596dc,
  0xe554001b, 0xfc4f315a, 0xd7626299, 0xce7953d8,
  0x49e14f17, 0x50fa7e56, 0x7bd72d95, 0x62cc1cd4,
  0x2d8d8a13, 0x3496bb52, 0x1fbbe891, 0x06a0d9d0,
  0x5e7ef3ec, 0x4765c2ad, 0x6c48916e, 0x7553a02f,
  0x3a1236e8, 0x230907a9, 0x0824546a, 0x113f652b,
  0x96a779e4, 0x8fbc48a5, 0xa4911b66, 0xbd8a2a27,
  0xf2cbbce0, 0xebd08da1, 0xc0fdde62, 0xd9e6ef23,
  0x14bce1bd, 0x0da7d0fc, 0x268a833f, 0x3f91b27e,
  0x70d024b9, 0x69cb15f8, 0x42e6463b, 0x5bfd777a,
  0xdc656bb5, 0xc57e5af4, 0xee530937, 0xf7483876,
  0xb809aeb1, 0xa1129ff0, 0x8a3fcc33, 0x9324fd72,
};

unsigned int crc32_table1[256] = 
{
  0x00000000, 0x01c26a37, 0x0384d46e, 0x0246be59,
  0x0709a8dc, 0x06cbc2eb, 0x048d7cb2, 0x054f1685,
  0x0e1351b8, 0x0fd13b8f, 0x0d9785d6, 0x0c55efe1,
  0x091af964, 0x08d89353, 0x0a9e2d0a, 0x0b5c473d,
  0x1c26a370, 0x1de4c947, 0x1fa2771e, 0x1e601d29,
  0x1b2f0bac, 0x1aed619b, 0x18abdfc2, 0x1969b5f5,
  0x1235f2c8, 0x13f798ff, 0x11b126a6, 0x10734c91,
  0x153c5a14, 0x14fe3023, 0x16b88e7a, 0x177ae44d,
  0x384d46e0, 0x398f2cd7, 0x3bc9928e, 0x3a0bf8b9,
  0x3f44ee3c, 0x3e86840b, 0x3cc03a52, 0x3d025065,
  0x365e1758, 0x379c7d6f, 0x35dac336, 0x3418a901,
  0x3157bf84, 0x3095d5b3, 0x32d36bea, 0x331101dd,
  0x246be590, 0x25a98fa7, 0x27ef31fe, 0x262d5bc9,
  0x23624d4c, 0x22a0277b, 0x20e69922, 0x2124f315,
  0x2a78b428, 0x2bbade1f, 0x29fc6046, 0x283e0a71,
  0x2d711cf4, 0x2cb376c3, 0x2ef5c89a, 0x2f37a2ad,
  0x709a8dc0, 0x7158e7f7, 0x731e59ae, 0x72dc3399,
  0x7793251c, 0x76514f2b, 0x7417f172, 0x75d59b45,
  0x7e89dc78, 0x7f4bb64f, 0x7d0d0816, 0x7ccf6221,
  0x798074a4, 0x78421e93, 0x7a04a0ca, 0x7bc6cafd,
  0x6cbc2eb0, 0x6d7e4487, 0x6f38fade, 0x6efa90e9,
  0x6bb5866c, 0x6a77ec5b, 0x68315202, 0x69f33835,
  0x62af7f08, 0x636d153f, 0x612bab66, 0x60e9c151,
  0x65a6d7d4, 0x6464bde3, 0x662203ba, 0x67e0698d,
  0x48d7cb20, 0x4915a117, 0x4b531f4e, 0x4a917579,
  0x4fde63fc, 0x4e1c09cb, 0x4c5ab792, 0x4d98dda5,
  0x46c49a98, 0x4706f0af, 0x45404ef6, 0x448224c1,
  0x41cd3244, 0x400f5873, 0x4249e62a, 0x438b8c1d,
  0x54f16850, 0x55330267, 0x5775bc3e, 0x56b7d609,
  0x53f8c08c, 0x523aaabb, 0x507c14e2, 0x51be7ed5,
  0x5ae239e8, 0x5b2053df, 0x5966ed86, 0x58a487b1,
  0x5deb9134, 0x5c29fb03, 0x5e6f455a, 0x5fad2f6d,
  0xe1351b80, 0xe0f771b7, 0xe2b1cfee, 0xe373a5d9,
  0xe63cb35c, 0xe7fed96b, 0xe5b86732, 0xe47a0d05,
  0xef264a38, 0xeee4200f, 0xeca29e56, 0xed60f461,
  0xe82fe2e4, 0xe9ed88d3, 0xebab368a, 0xea695cbd,
  0xfd13b8f0, 0xfcd1d2c7, 0xfe976c9e, 0xff5506a9,
  0xfa1a102c, 0xfbd87a1b, 0xf99ec442, 0xf85cae75,
  0xf300e948, 0xf2c2837f, 0xf0843d26, 0xf1465711,
  0xf4094194, 0xf5cb2ba3, 0xf78d95fa, 0xf64fffcd,
  0xd9785d60, 0xd8ba3757, 0xdafc890e, 0xdb3ee339,
  0xde71f5bc, 0xdfb39f8b, 0xddf521d2, 0xdc374be5,
  0xd76b0cd8, 0xd6a966ef, 0xd4efd8b6, 0xd52db281,
  0xd062a404, 0xd1a0ce33, 0xd3e6706a, 0xd2241a5d,
  0xc55efe10, 0xc49c9427, 0xc6da2a7e, 0xc7184049,
  0xc25756cc, 0xc3953cfb, 0xc1d382a2, 0xc011e895,
  0xcb4dafa8, 0xca8fc59f, 0xc8c97bc6, 0xc90b11f1,
  0xcc440774, 0xcd866d43, 0xcfc0d31a, 0xce02b92d,
  0x91af9640, 0x906dfc77, 0x922b422e, 0x93e92819,
  0x96a63e9c, 0x976454ab, 0x9522eaf2, 0x94e080c5,
  0x9fbcc7f8, 0x9e7eadcf, 0x9c381396, 0x9dfa79a1,
  0x98b56f24, 0x99770513, 0x9b31bb4a, 0x9af3d17d,
  0x8d893530, 0x8c4b5f07, 0x8e0de15e, 0x8fcf8b69,
  0x8a809dec, 0x8b42f7db, 0x89044982, 0x88c623b5,
  0x839a6488, 0x82580ebf, 0x801eb0e6, 0x81dcdad1,
  0x8493cc54, 0x8551a663, 0x8717183a, 0x86d5720d,
  0xa9e2d0a0, 0xa820ba97, 0xaa6604ce, 0xaba46ef9,
  0xaeeb787c, 0xaf29124b, 0xad6fac12, 0xacadc625,
  0xa7f18118, 0xa633eb2f, 0xa4755576, 0xa5b73f41,
  0xa0f829c4, 0xa13a43f3, 0xa37cfdaa, 0xa2be979d,
  0xb5c473d0, 0xb40619e7, 0xb640a7be, 0xb782cd89,
  0xb2cddb0c, 0xb30fb13b, 0xb1490f62, 0xb08b6555,
  0xbbd72268, 0xba15485f, 0xb853f606, 0xb9919c31,
  0xbcde8ab4, 0xbd1ce083, 0xbf5a5eda, 0xbe9834ed,
};

unsigned int crc32_table0[256] = {
  0x00000000, 0xb8bc6765, 0xaa09c88b, 0x12b5afee,
  0x8f629757, 0x37def032, 0x256b5fdc, 0x9dd738b9,
  0xc5b428ef, 0x7d084f8a, 0x6fbde064, 0xd7018701,
  0x4ad6bfb8, 0xf26ad8dd, 0xe0df7733, 0x58631056,
  0x5019579f, 0xe8a530fa, 0xfa109f14, 0x42acf871,
  0xdf7bc0c8, 0x67c7a7ad, 0x75720843, 0xcdce6f26,
  0x95ad7f70, 0x2d111815, 0x3fa4b7fb, 0x8718d09e,
  0x1acfe827, 0xa2738f42, 0xb0c620ac, 0x087a47c9,
  0xa032af3e, 0x188ec85b, 0x0a3b67b5, 0xb28700d0,
  0x2f503869, 0x97ec5f0c, 0x8559f0e2, 0x3de59787,
  0x658687d1, 0xdd3ae0b4, 0xcf8f4f5a, 0x7733283f,
  0xeae41086, 0x525877e3, 0x40edd80d, 0xf851bf68,
  0xf02bf8a1, 0x48979fc4, 0x5a22302a, 0xe29e574f,
  0x7f496ff6, 0xc7f50893, 0xd540a77d, 0x6dfcc018,
  0x359fd04e, 0x8d23b72b, 0x9f9618c5, 0x272a7fa0,
  0xbafd4719, 0x0241207c, 0x10f48f92, 0xa848e8f7,
  0x9b14583d, 0x23a83f58, 0x311d90b6, 0x89a1f7d3,
  0x1476cf6a, 0xaccaa80f, 0xbe7f07e1, 0x06c36084,
  0x5ea070d2, 0xe61c17b7, 0xf4a9b859, 0x4c15df3c,
  0xd1c2e785, 0x697e80e0, 0x7bcb2f0e, 0xc377486b,
  0xcb0d0fa2, 0x73b168c7, 0x6104c729, 0xd9b8a04c,
  0x446f98f5, 0xfcd3ff90, 0xee66507e, 0x56da371b,
  0x0eb9274d, 0xb6054028, 0xa4b0efc6, 0x1c0c88a3,
  0x81dbb01a, 0x3967d77f, 0x2bd27891, 0x936e1ff4,
  0x3b26f703, 0x839a9066, 0x912f3f88, 0x299358ed,
  0xb4446054, 0x0cf80731, 0x1e4da8df, 0xa6f1cfba,
  0xfe92dfec, 0x462eb889, 0x549b1767, 0xec277002,
  0x71f048bb, 0xc94c2fde, 0xdbf98030, 0x6345e755,
  0x6b3fa09c, 0xd383c7f9, 0xc1366817, 0x798a0f72,
  0xe45d37cb, 0x5ce150ae, 0x4e54ff40, 0xf6e89825,
  0xae8b8873, 0x1637ef16, 0x048240f8, 0xbc3e279d,
  0x21e91f24, 0x99557841, 0x8be0d7af, 0x335cb0ca,
  0xed59b63b, 0x55e5d15e, 0x47507eb0, 0xffec19d5,
  0x623b216c, 0xda874609, 0xc832e9e7, 0x708e8e82,
  0x28ed9ed4, 0x9051f9b1, 0x82e4565f, 0x3a58313a,
  0xa78f0983, 0x1f336ee6, 0x0d86c108, 0xb53aa66d,
  0xbd40e1a4, 0x05fc86c1, 0x1749292f, 0xaff54e4a,
  0x322276f3, 0x8a9e1196, 0x982bbe78, 0x2097d91d,
  0x78f4c94b, 0xc048ae2e, 0xd2fd01c0, 0x6a4166a5,
  0xf7965e1c, 0x4f2a3979, 0x5d9f9697, 0xe523f1f2,
  0x4d6b1905, 0xf5d77e60, 0xe762d18e, 0x5fdeb6eb,
  0xc2098e52, 0x7ab5e937, 0x680046d9, 0xd0bc21bc,
  0x88df31ea, 0x3063568f, 0x22d6f961, 0x9a6a9e04,
  0x07bda6bd, 0xbf01c1d8, 0xadb46e36, 0x15080953,
  0x1d724e9a, 0xa5ce29ff, 0xb77b8611, 0x0fc7e174,
  0x9210d9cd, 0x2aacbea8, 0x38191146, 0x80a57623,
  0xd8c66675, 0x607a0110, 0x72cfaefe, 0xca73c99b,
  0x57a4f122, 0xef189647, 0xfdad39a9, 0x45115ecc,
  0x764dee06, 0xcef18963, 0xdc44268d, 0x64f841e8,
  0xf92f7951, 0x41931e34, 0x5326b1da, 0xeb9ad6bf,
  0xb3f9c6e9, 0x0b45a18c, 0x19f00e62, 0xa14c6907,
  0x3c9b51be, 0x842736db, 0x96929935, 0x2e2efe50,
  0x2654b999, 0x9ee8defc, 0x8c5d7112, 0x34e11677,
  0xa9362ece, 0x118a49ab, 0x033fe645, 0xbb838120,
  0xe3e09176, 0x5b5cf613, 0x49e959fd, 0xf1553e98,
  0x6c820621, 0xd43e6144, 0xc68bceaa, 0x7e37a9cf,
  0xd67f4138, 0x6ec3265d, 0x7c7689b3, 0xc4caeed6,
  0x591dd66f, 0xe1a1b10a, 0xf3141ee4, 0x4ba87981,
  0x13cb69d7, 0xab770eb2, 0xb9c2a15c, 0x017ec639,
  0x9ca9fe80, 0x241599e5, 0x36a0360b, 0x8e1c516e,
  0x866616a7, 0x3eda71c2, 0x2c6fde2c, 0x94d3b949,
  0x090481f0, 0xb1b8e695, 0xa30d497b, 0x1bb12e1e,
  0x43d23e48, 0xfb6e592d, 0xe9dbf6c3, 0x516791a6,
  0xccb0a91f, 0x740cce7a, 0x66b96194, 0xde0506f1,
};

unsigned crc32_partial_old (const void *data, int len, unsigned crc) {
  const char *p = data;
  for (; len > 0; len--) {
    crc = crc32_table[(crc ^ *p++) & 0xff] ^ (crc >> 8);
  }
  return crc;    
}

/*
unsigned crc32_partial_fast (const void *data, int len, unsigned crc) {
  const int *p = (const int *) data;
  int x;
  for (x = (len >> 2); x > 0; x--) {
    crc ^= *p++;
    crc = crc32_table0[crc & 0xff] ^ crc32_table1[(crc & 0xff00) >> 8] ^ crc32_table2[(crc & 0xff0000) >> 16] ^ crc32_table[crc >> 24];
  }
  const char *q = (const char *) p;
  switch (len & 3) {
  case 3:
    crc = crc32_table[(crc ^ *q++) & 0xff] ^ (crc >> 8);
  case 2:
    crc = crc32_table[(crc ^ *q++) & 0xff] ^ (crc >> 8);
  case 1:
    crc = crc32_table[(crc ^ *q++) & 0xff] ^ (crc >> 8);
  }
  return crc;
}
*/

unsigned crc32_partial (const void *data, int len, unsigned crc) {
  const int *p = (const int *) data;
  int x;
#define DO_ONE(v) crc ^= v; crc = crc32_table0[crc & 0xff] ^ crc32_table1[(crc & 0xff00) >> 8] ^ crc32_table2[(crc & 0xff0000) >> 16] ^ crc32_table[crc >> 24];
#define DO_FOUR(p) DO_ONE((p)[0]); DO_ONE((p)[1]); DO_ONE((p)[2]); DO_ONE((p)[3]);

  for (x = (len >> 5); x > 0; x--) {
    DO_FOUR (p);
    DO_FOUR (p + 4);
    p += 8;
  }
  if (len & 16) {
    DO_FOUR (p);
    p += 4;
  }
  if (len & 8) {
    DO_ONE (p[0]);
    DO_ONE (p[1]);
    p += 2;
  }
  if (len & 4) {
    DO_ONE (*p++);
  }
  /*
  for (x = (len >> 2) & 7; x > 0; x--) {
    DO_ONE (*p++);
  }
  */
#undef DO_ONE
#undef DO_FOUR
  const char *q = (const char *) p;
  if (len & 2) {
    crc = crc32_table[(crc ^ q[0]) & 0xff] ^ (crc >> 8);
    crc = crc32_table[(crc ^ q[1]) & 0xff] ^ (crc >> 8);
    q += 2;
  }
  if (len & 1) {
    crc = crc32_table[(crc ^ *q++) & 0xff] ^ (crc >> 8);
  }
  return crc;
}

unsigned compute_crc32 (const void *data, int len) {
  return crc32_partial (data, len, -1) ^ -1;
}

unsigned long long crc64_table[256] = {
 0x0000000000000000LL, 0xb32e4cbe03a75f6fLL, 0xf4843657a840a05bLL, 0x47aa7ae9abe7ff34LL,
 0x7bd0c384ff8f5e33LL, 0xc8fe8f3afc28015cLL, 0x8f54f5d357cffe68LL, 0x3c7ab96d5468a107LL,
 0xf7a18709ff1ebc66LL, 0x448fcbb7fcb9e309LL, 0x0325b15e575e1c3dLL, 0xb00bfde054f94352LL,
 0x8c71448d0091e255LL, 0x3f5f08330336bd3aLL, 0x78f572daa8d1420eLL, 0xcbdb3e64ab761d61LL,
 0x7d9ba13851336649LL, 0xceb5ed8652943926LL, 0x891f976ff973c612LL, 0x3a31dbd1fad4997dLL,
 0x064b62bcaebc387aLL, 0xb5652e02ad1b6715LL, 0xf2cf54eb06fc9821LL, 0x41e11855055bc74eLL,
 0x8a3a2631ae2dda2fLL, 0x39146a8fad8a8540LL, 0x7ebe1066066d7a74LL, 0xcd905cd805ca251bLL,
 0xf1eae5b551a2841cLL, 0x42c4a90b5205db73LL, 0x056ed3e2f9e22447LL, 0xb6409f5cfa457b28LL,
 0xfb374270a266cc92LL, 0x48190ecea1c193fdLL, 0x0fb374270a266cc9LL, 0xbc9d3899098133a6LL,
 0x80e781f45de992a1LL, 0x33c9cd4a5e4ecdceLL, 0x7463b7a3f5a932faLL, 0xc74dfb1df60e6d95LL,
 0x0c96c5795d7870f4LL, 0xbfb889c75edf2f9bLL, 0xf812f32ef538d0afLL, 0x4b3cbf90f69f8fc0LL,
 0x774606fda2f72ec7LL, 0xc4684a43a15071a8LL, 0x83c230aa0ab78e9cLL, 0x30ec7c140910d1f3LL,
 0x86ace348f355aadbLL, 0x3582aff6f0f2f5b4LL, 0x7228d51f5b150a80LL, 0xc10699a158b255efLL,
 0xfd7c20cc0cdaf4e8LL, 0x4e526c720f7dab87LL, 0x09f8169ba49a54b3LL, 0xbad65a25a73d0bdcLL,
 0x710d64410c4b16bdLL, 0xc22328ff0fec49d2LL, 0x85895216a40bb6e6LL, 0x36a71ea8a7ace989LL,
 0x0adda7c5f3c4488eLL, 0xb9f3eb7bf06317e1LL, 0xfe5991925b84e8d5LL, 0x4d77dd2c5823b7baLL,
 0x64b62bcaebc387a1LL, 0xd7986774e864d8ceLL, 0x90321d9d438327faLL, 0x231c512340247895LL,
 0x1f66e84e144cd992LL, 0xac48a4f017eb86fdLL, 0xebe2de19bc0c79c9LL, 0x58cc92a7bfab26a6LL,
 0x9317acc314dd3bc7LL, 0x2039e07d177a64a8LL, 0x67939a94bc9d9b9cLL, 0xd4bdd62abf3ac4f3LL,
 0xe8c76f47eb5265f4LL, 0x5be923f9e8f53a9bLL, 0x1c4359104312c5afLL, 0xaf6d15ae40b59ac0LL,
 0x192d8af2baf0e1e8LL, 0xaa03c64cb957be87LL, 0xeda9bca512b041b3LL, 0x5e87f01b11171edcLL,
 0x62fd4976457fbfdbLL, 0xd1d305c846d8e0b4LL, 0x96797f21ed3f1f80LL, 0x2557339fee9840efLL,
 0xee8c0dfb45ee5d8eLL, 0x5da24145464902e1LL, 0x1a083bacedaefdd5LL, 0xa9267712ee09a2baLL,
 0x955cce7fba6103bdLL, 0x267282c1b9c65cd2LL, 0x61d8f8281221a3e6LL, 0xd2f6b4961186fc89LL,
 0x9f8169ba49a54b33LL, 0x2caf25044a02145cLL, 0x6b055fede1e5eb68LL, 0xd82b1353e242b407LL,
 0xe451aa3eb62a1500LL, 0x577fe680b58d4a6fLL, 0x10d59c691e6ab55bLL, 0xa3fbd0d71dcdea34LL,
 0x6820eeb3b6bbf755LL, 0xdb0ea20db51ca83aLL, 0x9ca4d8e41efb570eLL, 0x2f8a945a1d5c0861LL,
 0x13f02d374934a966LL, 0xa0de61894a93f609LL, 0xe7741b60e174093dLL, 0x545a57dee2d35652LL,
 0xe21ac88218962d7aLL, 0x5134843c1b317215LL, 0x169efed5b0d68d21LL, 0xa5b0b26bb371d24eLL,
 0x99ca0b06e7197349LL, 0x2ae447b8e4be2c26LL, 0x6d4e3d514f59d312LL, 0xde6071ef4cfe8c7dLL,
 0x15bb4f8be788911cLL, 0xa6950335e42fce73LL, 0xe13f79dc4fc83147LL, 0x521135624c6f6e28LL,
 0x6e6b8c0f1807cf2fLL, 0xdd45c0b11ba09040LL, 0x9aefba58b0476f74LL, 0x29c1f6e6b3e0301bLL,
 0xc96c5795d7870f42LL, 0x7a421b2bd420502dLL, 0x3de861c27fc7af19LL, 0x8ec62d7c7c60f076LL,
 0xb2bc941128085171LL, 0x0192d8af2baf0e1eLL, 0x4638a2468048f12aLL, 0xf516eef883efae45LL,
 0x3ecdd09c2899b324LL, 0x8de39c222b3eec4bLL, 0xca49e6cb80d9137fLL, 0x7967aa75837e4c10LL,
 0x451d1318d716ed17LL, 0xf6335fa6d4b1b278LL, 0xb199254f7f564d4cLL, 0x02b769f17cf11223LL,
 0xb4f7f6ad86b4690bLL, 0x07d9ba1385133664LL, 0x4073c0fa2ef4c950LL, 0xf35d8c442d53963fLL,
 0xcf273529793b3738LL, 0x7c0979977a9c6857LL, 0x3ba3037ed17b9763LL, 0x888d4fc0d2dcc80cLL,
 0x435671a479aad56dLL, 0xf0783d1a7a0d8a02LL, 0xb7d247f3d1ea7536LL, 0x04fc0b4dd24d2a59LL,
 0x3886b22086258b5eLL, 0x8ba8fe9e8582d431LL, 0xcc0284772e652b05LL, 0x7f2cc8c92dc2746aLL,
 0x325b15e575e1c3d0LL, 0x8175595b76469cbfLL, 0xc6df23b2dda1638bLL, 0x75f16f0cde063ce4LL,
 0x498bd6618a6e9de3LL, 0xfaa59adf89c9c28cLL, 0xbd0fe036222e3db8LL, 0x0e21ac88218962d7LL,
 0xc5fa92ec8aff7fb6LL, 0x76d4de52895820d9LL, 0x317ea4bb22bfdfedLL, 0x8250e80521188082LL,
 0xbe2a516875702185LL, 0x0d041dd676d77eeaLL, 0x4aae673fdd3081deLL, 0xf9802b81de97deb1LL,
 0x4fc0b4dd24d2a599LL, 0xfceef8632775faf6LL, 0xbb44828a8c9205c2LL, 0x086ace348f355aadLL,
 0x34107759db5dfbaaLL, 0x873e3be7d8faa4c5LL, 0xc094410e731d5bf1LL, 0x73ba0db070ba049eLL,
 0xb86133d4dbcc19ffLL, 0x0b4f7f6ad86b4690LL, 0x4ce50583738cb9a4LL, 0xffcb493d702be6cbLL,
 0xc3b1f050244347ccLL, 0x709fbcee27e418a3LL, 0x3735c6078c03e797LL, 0x841b8ab98fa4b8f8LL,
 0xadda7c5f3c4488e3LL, 0x1ef430e13fe3d78cLL, 0x595e4a08940428b8LL, 0xea7006b697a377d7LL,
 0xd60abfdbc3cbd6d0LL, 0x6524f365c06c89bfLL, 0x228e898c6b8b768bLL, 0x91a0c532682c29e4LL,
 0x5a7bfb56c35a3485LL, 0xe955b7e8c0fd6beaLL, 0xaeffcd016b1a94deLL, 0x1dd181bf68bdcbb1LL,
 0x21ab38d23cd56ab6LL, 0x9285746c3f7235d9LL, 0xd52f0e859495caedLL, 0x6601423b97329582LL,
 0xd041dd676d77eeaaLL, 0x636f91d96ed0b1c5LL, 0x24c5eb30c5374ef1LL, 0x97eba78ec690119eLL,
 0xab911ee392f8b099LL, 0x18bf525d915feff6LL, 0x5f1528b43ab810c2LL, 0xec3b640a391f4fadLL,
 0x27e05a6e926952ccLL, 0x94ce16d091ce0da3LL, 0xd3646c393a29f297LL, 0x604a2087398eadf8LL,
 0x5c3099ea6de60cffLL, 0xef1ed5546e415390LL, 0xa8b4afbdc5a6aca4LL, 0x1b9ae303c601f3cbLL,
 0x56ed3e2f9e224471LL, 0xe5c372919d851b1eLL, 0xa26908783662e42aLL, 0x114744c635c5bb45LL,
 0x2d3dfdab61ad1a42LL, 0x9e13b115620a452dLL, 0xd9b9cbfcc9edba19LL, 0x6a978742ca4ae576LL,
 0xa14cb926613cf817LL, 0x1262f598629ba778LL, 0x55c88f71c97c584cLL, 0xe6e6c3cfcadb0723LL,
 0xda9c7aa29eb3a624LL, 0x69b2361c9d14f94bLL, 0x2e184cf536f3067fLL, 0x9d36004b35545910LL,
 0x2b769f17cf112238LL, 0x9858d3a9ccb67d57LL, 0xdff2a94067518263LL, 0x6cdce5fe64f6dd0cLL,
 0x50a65c93309e7c0bLL, 0xe388102d33392364LL, 0xa4226ac498dedc50LL, 0x170c267a9b79833fLL,
 0xdcd7181e300f9e5eLL, 0x6ff954a033a8c131LL, 0x28532e49984f3e05LL, 0x9b7d62f79be8616aLL,
 0xa707db9acf80c06dLL, 0x14299724cc279f02LL, 0x5383edcd67c06036LL, 0xe0ada17364673f59LL
};

unsigned long long crc64_partial (const void *data, int len, unsigned long long crc) {
  const char *p = data;
  for (; len > 0; len--) {
    crc = crc64_table[(crc ^ *p++) & 0xff] ^ (crc >> 8);
  }
  return crc;    
}

unsigned long long crc64 (const void *data, int len) {
  return crc64_partial (data, len, -1LL) ^ -1LL;
}

static unsigned gf32_matrix_times (unsigned *matrix, unsigned vector) {
  unsigned sum = 0;
  while (vector) {
    if (vector & 1) {
      sum ^= *matrix;
    }
    vector >>= 1;
    matrix++;
  }
  return sum;
}

static void gf32_matrix_square (unsigned *square, unsigned *matrix) {
  int n = 0;
  do {
    square[n] = gf32_matrix_times (matrix, matrix[n]);
  } while (++n < 32);
}

unsigned compute_crc32_combine (unsigned crc1, unsigned crc2, int len2) {
  assert (len2 < (1 << 29));
  static int power_buf_initialized = 0;
  static unsigned power_buf[1024];
  int n;
  /* degenerate case (also disallow negative lengths) */
  if (len2 <= 0) {
    return crc1;
  }
  if (!power_buf_initialized) {
    power_buf[0] = 0xedb88320UL;
    for (n = 0; n < 31; n++) {
      power_buf[n+1] = 1U << n;
    }
    for (n = 1; n < 32; n++) {
      gf32_matrix_square (power_buf + (n << 5), power_buf + ((n - 1) << 5));
    }
    power_buf_initialized = 1;
  }

  unsigned int *p = power_buf + 64;
  do {
    p += 32;
    if (len2 & 1) {
      crc1 = gf32_matrix_times (p, crc1);
    }
    len2 >>= 1;
  } while (len2);
  return crc1 ^ crc2;
}


/********************************* crc32 repair ************************/
struct fcb_table_entry {
  unsigned p; //zeta ^ k
  int i;
};

static inline unsigned gf32_mod (unsigned long long r, int high_bit) {
  int j = high_bit;
  for (j = high_bit; j >= 32; j--) {
    if ((1ULL << j) & r) {
      r ^= 0x04C11DB7ULL << (j - 32);
    }
  }
  return (unsigned) r;
}

static unsigned gf32_mult (unsigned a, unsigned b) {
  int i;
  const unsigned long long m = b;
  unsigned long long r = 0;
  for (i = 0; i < 32; i++) {
    if (a & (1U << i)) {
      r ^= m << i;
    }
  }
  return gf32_mod (r, 62);
}

static unsigned gf32_shl (unsigned int a, int shift) {
  unsigned long long r = a;
  r <<= shift;
  return gf32_mod (r, 31 + shift);
}

static unsigned gf32_pow (unsigned a, int k) {
  if (!k) { return 1; }
  unsigned x = gf32_pow (gf32_mult (a, a), k >> 1);
  if (k & 1) {
    x = gf32_mult (x, a);
  }
  return x;
}

static int cmp_fcb_table_entry (const void *a, const void *b) {
  const struct fcb_table_entry *x = a;
  const struct fcb_table_entry *y = b;
  if (x->p < y->p) { return -1; }
  if (x->p > y->p) { return  1; }
  if (x->i < y->i) { return -1; }
  if (x->i > y->i) { return  1; }
  return 0;
}

#define GROUP_SWAP(x,m,s) ((x & m) << s) | ((x & (~m)) >> s)
static unsigned revbin (unsigned x) {
  x = GROUP_SWAP(x,0x55555555U,1);
  x = GROUP_SWAP(x,0x33333333U,2);
  x = GROUP_SWAP(x,0x0f0f0f0fU,4);
  x = __builtin_bswap32 (x);
  return x;
}
#undef GROUP_SWAP

static inline unsigned xmult (unsigned a) {
  unsigned r = a << 1;
  if (a & (1U<<31)) {
    r ^= 0x04C11DB7U;
  }
  return r;
}

static int find_corrupted_bit (int size, unsigned d) {
  int i, j;
  size += 4;
  d = revbin (d);
  int n = size << 3;
  int r = (int) (sqrt (n) + 0.5);
  struct fcb_table_entry *T = calloc (r, sizeof (struct fcb_table_entry));
  assert (T != NULL);
  T[0].i = 0;
  T[0].p = 1;
  for (i = 1; i < r; i++) {
    T[i].i = i;
    T[i].p = xmult (T[i-1].p);
  }
  assert (xmult (0x82608EDB) == 1);
  qsort (T, r, sizeof (T[0]), cmp_fcb_table_entry);
  unsigned q = gf32_pow (0x82608EDB, r);

  unsigned A[32];
  for (i = 0; i < 32; i++) {
    A[i] = gf32_shl (q, i);
  }

  unsigned x = d;
  int max_j = n / r, res = -1;
  for (j = 0; j <= max_j; j++) {
    int a = -1, b = r;
    while (b - a > 1) {
      int c = ((a + b) >> 1);
      if (T[c].p <= x) { a = c; } else { b = c; }
    }
    if (a >= 0 && T[a].p == x) {
      res = T[a].i + r * j;
      break;
    }
    x = gf32_matrix_times (A, x);
  }
  free (T);
  return res;
}

static int repair_bit (unsigned char *input, int l, int k) {
  if (k < 0) {
    return -1;
  }
  int idx = k >> 5, bit = k & 31, i = (l - 1) - (idx - 1) * 4;
  while (bit >= 8) {
    i--;
    bit -= 8;
  }
  if (i < 0) {
    return -2;
  }
  if (i >= l) {
    return -3;
  }
  int j = 7 - bit;
  input[i] ^= 1 << j;
  return 0;
}

int crc32_check_and_repair (void *input, int l, unsigned *input_crc32, int force_exit) {
  unsigned computed_crc32 = compute_crc32 (input, l);
  const unsigned crc32_diff = computed_crc32 ^ (*input_crc32);
  if (!crc32_diff) {
    return 0;
  }
  int k = find_corrupted_bit (l, crc32_diff);
  int r = repair_bit (input, l, k);
  if (!r) {
    assert (compute_crc32 (input, l) == *input_crc32);
    return 1;
  }
  if (!(crc32_diff & (crc32_diff - 1))) { /* crc32_diff is power of 2 */
    *input_crc32 = computed_crc32;
    return 2;
  }
  assert (!force_exit);
  *input_crc32 = computed_crc32;
  return -1;
}
