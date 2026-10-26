#![allow(dead_code, unused_imports)]
pub mod c00 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I00; } }
pub mod c01 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I01; } }
pub mod c02 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I02; } }
pub mod c03 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I03; } }
pub mod c04 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I04; } }
pub mod c05 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I05; } }
pub mod c06 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I06; } }
pub mod c07 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I07; } }
pub mod c08 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I08; } }
pub mod c09 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I09; } }
pub mod c10 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I10; } }
pub mod c11 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I11; } }
pub mod c12 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I12; } }
pub mod c13 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I13; } }
pub mod c14 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I14; } }
pub mod c15 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I15; } }
pub mod c16 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I16; } }
pub mod c17 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I17; } }
pub mod c18 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I18; } }
pub mod c19 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I19; } }
pub mod c20 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I20; } }
pub mod c21 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I21; } }
pub mod c22 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I22; } }
pub mod c23 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I23; } }
pub mod c24 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I24; } }
pub mod c25 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I25; } }
pub mod c26 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I26; } }
pub mod c27 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I27; } }
pub mod c28 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I28; } }
pub mod c29 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I29; } }
pub mod c30 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I30; } }
pub mod c31 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I31; } }
pub mod c32 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I32; } }
pub mod c33 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I33; } }
pub mod c34 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I34; } }
pub mod c35 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct I35; } }
pub mod math {
    pub mod prelude { pub use crate::math::primitives::*; }
    pub mod primitives {
        mod dim3 {
            pub struct Cuboid(pub u8);
            impl Cuboid {
                pub fn new(x: u8) -> Self {
                    println!("Cuboid::new\n{}", std::backtrace::Backtrace::force_capture());
                    Cuboid(x)
                }
            }
            impl Default for Cuboid {
                fn default() -> Self {
                    println!("Cuboid::default\n{}", std::backtrace::Backtrace::force_capture());
                    Cuboid(0)
                }
            }
        }
        pub use dim3::*;
    }
}
pub mod gizmo {
    pub struct Cuboid(pub u8);
    impl Cuboid {
        pub fn new(x: u8) -> Self { Cuboid(x) }
    }
    impl Default for Cuboid {
        fn default() -> Self { Cuboid(1) }
    }
}
pub mod prelude {
    pub use crate::math::prelude::*;
    pub use crate::c00::prelude::*;
    pub use crate::c01::prelude::*;
    pub use crate::c02::prelude::*;
    pub use crate::c03::prelude::*;
    pub use crate::c04::prelude::*;
    pub use crate::c05::prelude::*;
    pub use crate::c06::prelude::*;
    pub use crate::c07::prelude::*;
    pub use crate::c08::prelude::*;
    pub use crate::c09::prelude::*;
    pub use crate::c10::prelude::*;
    pub use crate::c11::prelude::*;
    pub use crate::c12::prelude::*;
    pub use crate::c13::prelude::*;
    pub use crate::c14::prelude::*;
    pub use crate::c15::prelude::*;
    pub use crate::c16::prelude::*;
    pub use crate::c17::prelude::*;
    pub use crate::c18::prelude::*;
    pub use crate::c19::prelude::*;
    pub use crate::c20::prelude::*;
    pub use crate::c21::prelude::*;
    pub use crate::c22::prelude::*;
    pub use crate::c23::prelude::*;
    pub use crate::c24::prelude::*;
    pub use crate::c25::prelude::*;
    pub use crate::c26::prelude::*;
    pub use crate::c27::prelude::*;
    pub use crate::c28::prelude::*;
    pub use crate::c29::prelude::*;
    pub use crate::c30::prelude::*;
    pub use crate::c31::prelude::*;
    pub use crate::c32::prelude::*;
    pub use crate::c33::prelude::*;
    pub use crate::c34::prelude::*;
    pub use crate::c35::prelude::*;
}

pub mod wide {
    pub mod w000 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W000; } }
    pub mod w001 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W001; } }
    pub mod w002 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W002; } }
    pub mod w003 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W003; } }
    pub mod w004 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W004; } }
    pub mod w005 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W005; } }
    pub mod w006 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W006; } }
    pub mod w007 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W007; } }
    pub mod w008 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W008; } }
    pub mod w009 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W009; } }
    pub mod w010 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W010; } }
    pub mod w011 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W011; } }
    pub mod w012 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W012; } }
    pub mod w013 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W013; } }
    pub mod w014 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W014; } }
    pub mod w015 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W015; } }
    pub mod w016 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W016; } }
    pub mod w017 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W017; } }
    pub mod w018 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W018; } }
    pub mod w019 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W019; } }
    pub mod w020 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W020; } }
    pub mod w021 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W021; } }
    pub mod w022 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W022; } }
    pub mod w023 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W023; } }
    pub mod w024 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W024; } }
    pub mod w025 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W025; } }
    pub mod w026 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W026; } }
    pub mod w027 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W027; } }
    pub mod w028 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W028; } }
    pub mod w029 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W029; } }
    pub mod w030 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W030; } }
    pub mod w031 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W031; } }
    pub mod w032 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W032; } }
    pub mod w033 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W033; } }
    pub mod w034 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W034; } }
    pub mod w035 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W035; } }
    pub mod w036 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W036; } }
    pub mod w037 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W037; } }
    pub mod w038 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W038; } }
    pub mod w039 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W039; } }
    pub mod w040 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W040; } }
    pub mod w041 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W041; } }
    pub mod w042 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W042; } }
    pub mod w043 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W043; } }
    pub mod w044 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W044; } }
    pub mod w045 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W045; } }
    pub mod w046 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W046; } }
    pub mod w047 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W047; } }
    pub mod w048 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W048; } }
    pub mod w049 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W049; } }
    pub mod w050 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W050; } }
    pub mod w051 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W051; } }
    pub mod w052 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W052; } }
    pub mod w053 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W053; } }
    pub mod w054 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W054; } }
    pub mod w055 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W055; } }
    pub mod w056 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W056; } }
    pub mod w057 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W057; } }
    pub mod w058 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W058; } }
    pub mod w059 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W059; } }
    pub mod w060 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W060; } }
    pub mod w061 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W061; } }
    pub mod w062 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W062; } }
    pub mod w063 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W063; } }
    pub mod w064 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W064; } }
    pub mod w065 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W065; } }
    pub mod w066 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W066; } }
    pub mod w067 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W067; } }
    pub mod w068 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W068; } }
    pub mod w069 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W069; } }
    pub mod w070 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W070; } }
    pub mod w071 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W071; } }
    pub mod w072 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W072; } }
    pub mod w073 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W073; } }
    pub mod w074 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W074; } }
    pub mod w075 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W075; } }
    pub mod w076 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W076; } }
    pub mod w077 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W077; } }
    pub mod w078 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W078; } }
    pub mod w079 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W079; } }
    pub mod w080 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W080; } }
    pub mod w081 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W081; } }
    pub mod w082 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W082; } }
    pub mod w083 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W083; } }
    pub mod w084 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W084; } }
    pub mod w085 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W085; } }
    pub mod w086 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W086; } }
    pub mod w087 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W087; } }
    pub mod w088 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W088; } }
    pub mod w089 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W089; } }
    pub mod w090 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W090; } }
    pub mod w091 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W091; } }
    pub mod w092 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W092; } }
    pub mod w093 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W093; } }
    pub mod w094 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W094; } }
    pub mod w095 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W095; } }
    pub mod w096 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W096; } }
    pub mod w097 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W097; } }
    pub mod w098 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W098; } }
    pub mod w099 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W099; } }
    pub mod w100 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W100; } }
    pub mod w101 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W101; } }
    pub mod w102 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W102; } }
    pub mod w103 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W103; } }
    pub mod w104 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W104; } }
    pub mod w105 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W105; } }
    pub mod w106 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W106; } }
    pub mod w107 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W107; } }
    pub mod w108 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W108; } }
    pub mod w109 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W109; } }
    pub mod w110 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W110; } }
    pub mod w111 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W111; } }
    pub mod w112 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W112; } }
    pub mod w113 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W113; } }
    pub mod w114 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W114; } }
    pub mod w115 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W115; } }
    pub mod w116 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W116; } }
    pub mod w117 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W117; } }
    pub mod w118 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W118; } }
    pub mod w119 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W119; } }
    pub mod w120 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W120; } }
    pub mod w121 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W121; } }
    pub mod w122 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W122; } }
    pub mod w123 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W123; } }
    pub mod w124 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W124; } }
    pub mod w125 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W125; } }
    pub mod w126 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W126; } }
    pub mod w127 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W127; } }
    pub mod w128 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W128; } }
    pub mod w129 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W129; } }
    pub mod w130 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W130; } }
    pub mod w131 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W131; } }
    pub mod w132 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W132; } }
    pub mod w133 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W133; } }
    pub mod w134 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W134; } }
    pub mod w135 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W135; } }
    pub mod w136 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W136; } }
    pub mod w137 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W137; } }
    pub mod w138 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W138; } }
    pub mod w139 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W139; } }
    pub mod w140 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W140; } }
    pub mod w141 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W141; } }
    pub mod w142 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W142; } }
    pub mod w143 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W143; } }
    pub mod w144 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W144; } }
    pub mod w145 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W145; } }
    pub mod w146 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W146; } }
    pub mod w147 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W147; } }
    pub mod w148 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W148; } }
    pub mod w149 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W149; } }
    pub mod w150 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W150; } }
    pub mod w151 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W151; } }
    pub mod w152 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W152; } }
    pub mod w153 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W153; } }
    pub mod w154 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W154; } }
    pub mod w155 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W155; } }
    pub mod w156 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W156; } }
    pub mod w157 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W157; } }
    pub mod w158 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W158; } }
    pub mod w159 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W159; } }
    pub mod w160 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W160; } }
    pub mod w161 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W161; } }
    pub mod w162 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W162; } }
    pub mod w163 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W163; } }
    pub mod w164 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W164; } }
    pub mod w165 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W165; } }
    pub mod w166 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W166; } }
    pub mod w167 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W167; } }
    pub mod w168 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W168; } }
    pub mod w169 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W169; } }
    pub mod w170 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W170; } }
    pub mod w171 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W171; } }
    pub mod w172 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W172; } }
    pub mod w173 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W173; } }
    pub mod w174 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W174; } }
    pub mod w175 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W175; } }
    pub mod w176 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W176; } }
    pub mod w177 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W177; } }
    pub mod w178 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W178; } }
    pub mod w179 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W179; } }
    pub mod w180 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W180; } }
    pub mod w181 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W181; } }
    pub mod w182 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W182; } }
    pub mod w183 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W183; } }
    pub mod w184 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W184; } }
    pub mod w185 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W185; } }
    pub mod w186 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W186; } }
    pub mod w187 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W187; } }
    pub mod w188 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W188; } }
    pub mod w189 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W189; } }
    pub mod w190 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W190; } }
    pub mod w191 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W191; } }
    pub mod w192 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W192; } }
    pub mod w193 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W193; } }
    pub mod w194 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W194; } }
    pub mod w195 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W195; } }
    pub mod w196 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W196; } }
    pub mod w197 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W197; } }
    pub mod w198 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W198; } }
    pub mod w199 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W199; } }
    pub mod w200 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W200; } }
    pub mod w201 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W201; } }
    pub mod w202 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W202; } }
    pub mod w203 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W203; } }
    pub mod w204 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W204; } }
    pub mod w205 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W205; } }
    pub mod w206 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W206; } }
    pub mod w207 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W207; } }
    pub mod w208 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W208; } }
    pub mod w209 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W209; } }
    pub mod w210 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W210; } }
    pub mod w211 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W211; } }
    pub mod w212 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W212; } }
    pub mod w213 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W213; } }
    pub mod w214 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W214; } }
    pub mod w215 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W215; } }
    pub mod w216 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W216; } }
    pub mod w217 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W217; } }
    pub mod w218 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W218; } }
    pub mod w219 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W219; } }
    pub mod w220 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W220; } }
    pub mod w221 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W221; } }
    pub mod w222 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W222; } }
    pub mod w223 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W223; } }
    pub mod w224 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W224; } }
    pub mod w225 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W225; } }
    pub mod w226 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W226; } }
    pub mod w227 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W227; } }
    pub mod w228 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W228; } }
    pub mod w229 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W229; } }
    pub mod w230 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W230; } }
    pub mod w231 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W231; } }
    pub mod w232 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W232; } }
    pub mod w233 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W233; } }
    pub mod w234 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W234; } }
    pub mod w235 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W235; } }
    pub mod w236 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W236; } }
    pub mod w237 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W237; } }
    pub mod w238 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W238; } }
    pub mod w239 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W239; } }
    pub mod w240 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W240; } }
    pub mod w241 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W241; } }
    pub mod w242 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W242; } }
    pub mod w243 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W243; } }
    pub mod w244 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W244; } }
    pub mod w245 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W245; } }
    pub mod w246 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W246; } }
    pub mod w247 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W247; } }
    pub mod w248 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W248; } }
    pub mod w249 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W249; } }
    pub mod w250 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W250; } }
    pub mod w251 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W251; } }
    pub mod w252 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W252; } }
    pub mod w253 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W253; } }
    pub mod w254 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W254; } }
    pub mod w255 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W255; } }
    pub mod w256 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W256; } }
    pub mod w257 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W257; } }
    pub mod w258 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W258; } }
    pub mod w259 { pub mod prelude { pub use super::items::*; } pub mod items { pub struct W259; } }
    pub mod mesh {
        pub struct Mesh(pub u8);
        impl Mesh {
            pub fn new(x: u8) -> Self {
                println!("Mesh::new\n{}", std::backtrace::Backtrace::force_capture());
                Mesh(x)
            }
        }
    }
    pub mod render {
        pub mod prelude { pub use crate::wide::mesh::Mesh; }
    }
    pub mod prelude {
        pub use crate::wide::render::prelude::*;
        pub use crate::wide::w000::prelude::*;
        pub use crate::wide::w001::prelude::*;
        pub use crate::wide::w002::prelude::*;
        pub use crate::wide::w003::prelude::*;
        pub use crate::wide::w004::prelude::*;
        pub use crate::wide::w005::prelude::*;
        pub use crate::wide::w006::prelude::*;
        pub use crate::wide::w007::prelude::*;
        pub use crate::wide::w008::prelude::*;
        pub use crate::wide::w009::prelude::*;
        pub use crate::wide::w010::prelude::*;
        pub use crate::wide::w011::prelude::*;
        pub use crate::wide::w012::prelude::*;
        pub use crate::wide::w013::prelude::*;
        pub use crate::wide::w014::prelude::*;
        pub use crate::wide::w015::prelude::*;
        pub use crate::wide::w016::prelude::*;
        pub use crate::wide::w017::prelude::*;
        pub use crate::wide::w018::prelude::*;
        pub use crate::wide::w019::prelude::*;
        pub use crate::wide::w020::prelude::*;
        pub use crate::wide::w021::prelude::*;
        pub use crate::wide::w022::prelude::*;
        pub use crate::wide::w023::prelude::*;
        pub use crate::wide::w024::prelude::*;
        pub use crate::wide::w025::prelude::*;
        pub use crate::wide::w026::prelude::*;
        pub use crate::wide::w027::prelude::*;
        pub use crate::wide::w028::prelude::*;
        pub use crate::wide::w029::prelude::*;
        pub use crate::wide::w030::prelude::*;
        pub use crate::wide::w031::prelude::*;
        pub use crate::wide::w032::prelude::*;
        pub use crate::wide::w033::prelude::*;
        pub use crate::wide::w034::prelude::*;
        pub use crate::wide::w035::prelude::*;
        pub use crate::wide::w036::prelude::*;
        pub use crate::wide::w037::prelude::*;
        pub use crate::wide::w038::prelude::*;
        pub use crate::wide::w039::prelude::*;
        pub use crate::wide::w040::prelude::*;
        pub use crate::wide::w041::prelude::*;
        pub use crate::wide::w042::prelude::*;
        pub use crate::wide::w043::prelude::*;
        pub use crate::wide::w044::prelude::*;
        pub use crate::wide::w045::prelude::*;
        pub use crate::wide::w046::prelude::*;
        pub use crate::wide::w047::prelude::*;
        pub use crate::wide::w048::prelude::*;
        pub use crate::wide::w049::prelude::*;
        pub use crate::wide::w050::prelude::*;
        pub use crate::wide::w051::prelude::*;
        pub use crate::wide::w052::prelude::*;
        pub use crate::wide::w053::prelude::*;
        pub use crate::wide::w054::prelude::*;
        pub use crate::wide::w055::prelude::*;
        pub use crate::wide::w056::prelude::*;
        pub use crate::wide::w057::prelude::*;
        pub use crate::wide::w058::prelude::*;
        pub use crate::wide::w059::prelude::*;
        pub use crate::wide::w060::prelude::*;
        pub use crate::wide::w061::prelude::*;
        pub use crate::wide::w062::prelude::*;
        pub use crate::wide::w063::prelude::*;
        pub use crate::wide::w064::prelude::*;
        pub use crate::wide::w065::prelude::*;
        pub use crate::wide::w066::prelude::*;
        pub use crate::wide::w067::prelude::*;
        pub use crate::wide::w068::prelude::*;
        pub use crate::wide::w069::prelude::*;
        pub use crate::wide::w070::prelude::*;
        pub use crate::wide::w071::prelude::*;
        pub use crate::wide::w072::prelude::*;
        pub use crate::wide::w073::prelude::*;
        pub use crate::wide::w074::prelude::*;
        pub use crate::wide::w075::prelude::*;
        pub use crate::wide::w076::prelude::*;
        pub use crate::wide::w077::prelude::*;
        pub use crate::wide::w078::prelude::*;
        pub use crate::wide::w079::prelude::*;
        pub use crate::wide::w080::prelude::*;
        pub use crate::wide::w081::prelude::*;
        pub use crate::wide::w082::prelude::*;
        pub use crate::wide::w083::prelude::*;
        pub use crate::wide::w084::prelude::*;
        pub use crate::wide::w085::prelude::*;
        pub use crate::wide::w086::prelude::*;
        pub use crate::wide::w087::prelude::*;
        pub use crate::wide::w088::prelude::*;
        pub use crate::wide::w089::prelude::*;
        pub use crate::wide::w090::prelude::*;
        pub use crate::wide::w091::prelude::*;
        pub use crate::wide::w092::prelude::*;
        pub use crate::wide::w093::prelude::*;
        pub use crate::wide::w094::prelude::*;
        pub use crate::wide::w095::prelude::*;
        pub use crate::wide::w096::prelude::*;
        pub use crate::wide::w097::prelude::*;
        pub use crate::wide::w098::prelude::*;
        pub use crate::wide::w099::prelude::*;
        pub use crate::wide::w100::prelude::*;
        pub use crate::wide::w101::prelude::*;
        pub use crate::wide::w102::prelude::*;
        pub use crate::wide::w103::prelude::*;
        pub use crate::wide::w104::prelude::*;
        pub use crate::wide::w105::prelude::*;
        pub use crate::wide::w106::prelude::*;
        pub use crate::wide::w107::prelude::*;
        pub use crate::wide::w108::prelude::*;
        pub use crate::wide::w109::prelude::*;
        pub use crate::wide::w110::prelude::*;
        pub use crate::wide::w111::prelude::*;
        pub use crate::wide::w112::prelude::*;
        pub use crate::wide::w113::prelude::*;
        pub use crate::wide::w114::prelude::*;
        pub use crate::wide::w115::prelude::*;
        pub use crate::wide::w116::prelude::*;
        pub use crate::wide::w117::prelude::*;
        pub use crate::wide::w118::prelude::*;
        pub use crate::wide::w119::prelude::*;
        pub use crate::wide::w120::prelude::*;
        pub use crate::wide::w121::prelude::*;
        pub use crate::wide::w122::prelude::*;
        pub use crate::wide::w123::prelude::*;
        pub use crate::wide::w124::prelude::*;
        pub use crate::wide::w125::prelude::*;
        pub use crate::wide::w126::prelude::*;
        pub use crate::wide::w127::prelude::*;
        pub use crate::wide::w128::prelude::*;
        pub use crate::wide::w129::prelude::*;
        pub use crate::wide::w130::prelude::*;
        pub use crate::wide::w131::prelude::*;
        pub use crate::wide::w132::prelude::*;
        pub use crate::wide::w133::prelude::*;
        pub use crate::wide::w134::prelude::*;
        pub use crate::wide::w135::prelude::*;
        pub use crate::wide::w136::prelude::*;
        pub use crate::wide::w137::prelude::*;
        pub use crate::wide::w138::prelude::*;
        pub use crate::wide::w139::prelude::*;
        pub use crate::wide::w140::prelude::*;
        pub use crate::wide::w141::prelude::*;
        pub use crate::wide::w142::prelude::*;
        pub use crate::wide::w143::prelude::*;
        pub use crate::wide::w144::prelude::*;
        pub use crate::wide::w145::prelude::*;
        pub use crate::wide::w146::prelude::*;
        pub use crate::wide::w147::prelude::*;
        pub use crate::wide::w148::prelude::*;
        pub use crate::wide::w149::prelude::*;
        pub use crate::wide::w150::prelude::*;
        pub use crate::wide::w151::prelude::*;
        pub use crate::wide::w152::prelude::*;
        pub use crate::wide::w153::prelude::*;
        pub use crate::wide::w154::prelude::*;
        pub use crate::wide::w155::prelude::*;
        pub use crate::wide::w156::prelude::*;
        pub use crate::wide::w157::prelude::*;
        pub use crate::wide::w158::prelude::*;
        pub use crate::wide::w159::prelude::*;
        pub use crate::wide::w160::prelude::*;
        pub use crate::wide::w161::prelude::*;
        pub use crate::wide::w162::prelude::*;
        pub use crate::wide::w163::prelude::*;
        pub use crate::wide::w164::prelude::*;
        pub use crate::wide::w165::prelude::*;
        pub use crate::wide::w166::prelude::*;
        pub use crate::wide::w167::prelude::*;
        pub use crate::wide::w168::prelude::*;
        pub use crate::wide::w169::prelude::*;
        pub use crate::wide::w170::prelude::*;
        pub use crate::wide::w171::prelude::*;
        pub use crate::wide::w172::prelude::*;
        pub use crate::wide::w173::prelude::*;
        pub use crate::wide::w174::prelude::*;
        pub use crate::wide::w175::prelude::*;
        pub use crate::wide::w176::prelude::*;
        pub use crate::wide::w177::prelude::*;
        pub use crate::wide::w178::prelude::*;
        pub use crate::wide::w179::prelude::*;
        pub use crate::wide::w180::prelude::*;
        pub use crate::wide::w181::prelude::*;
        pub use crate::wide::w182::prelude::*;
        pub use crate::wide::w183::prelude::*;
        pub use crate::wide::w184::prelude::*;
        pub use crate::wide::w185::prelude::*;
        pub use crate::wide::w186::prelude::*;
        pub use crate::wide::w187::prelude::*;
        pub use crate::wide::w188::prelude::*;
        pub use crate::wide::w189::prelude::*;
        pub use crate::wide::w190::prelude::*;
        pub use crate::wide::w191::prelude::*;
        pub use crate::wide::w192::prelude::*;
        pub use crate::wide::w193::prelude::*;
        pub use crate::wide::w194::prelude::*;
        pub use crate::wide::w195::prelude::*;
        pub use crate::wide::w196::prelude::*;
        pub use crate::wide::w197::prelude::*;
        pub use crate::wide::w198::prelude::*;
        pub use crate::wide::w199::prelude::*;
        pub use crate::wide::w200::prelude::*;
        pub use crate::wide::w201::prelude::*;
        pub use crate::wide::w202::prelude::*;
        pub use crate::wide::w203::prelude::*;
        pub use crate::wide::w204::prelude::*;
        pub use crate::wide::w205::prelude::*;
        pub use crate::wide::w206::prelude::*;
        pub use crate::wide::w207::prelude::*;
        pub use crate::wide::w208::prelude::*;
        pub use crate::wide::w209::prelude::*;
        pub use crate::wide::w210::prelude::*;
        pub use crate::wide::w211::prelude::*;
        pub use crate::wide::w212::prelude::*;
        pub use crate::wide::w213::prelude::*;
        pub use crate::wide::w214::prelude::*;
        pub use crate::wide::w215::prelude::*;
        pub use crate::wide::w216::prelude::*;
        pub use crate::wide::w217::prelude::*;
        pub use crate::wide::w218::prelude::*;
        pub use crate::wide::w219::prelude::*;
        pub use crate::wide::w220::prelude::*;
        pub use crate::wide::w221::prelude::*;
        pub use crate::wide::w222::prelude::*;
        pub use crate::wide::w223::prelude::*;
        pub use crate::wide::w224::prelude::*;
        pub use crate::wide::w225::prelude::*;
        pub use crate::wide::w226::prelude::*;
        pub use crate::wide::w227::prelude::*;
        pub use crate::wide::w228::prelude::*;
        pub use crate::wide::w229::prelude::*;
        pub use crate::wide::w230::prelude::*;
        pub use crate::wide::w231::prelude::*;
        pub use crate::wide::w232::prelude::*;
        pub use crate::wide::w233::prelude::*;
        pub use crate::wide::w234::prelude::*;
        pub use crate::wide::w235::prelude::*;
        pub use crate::wide::w236::prelude::*;
        pub use crate::wide::w237::prelude::*;
        pub use crate::wide::w238::prelude::*;
        pub use crate::wide::w239::prelude::*;
        pub use crate::wide::w240::prelude::*;
        pub use crate::wide::w241::prelude::*;
        pub use crate::wide::w242::prelude::*;
        pub use crate::wide::w243::prelude::*;
        pub use crate::wide::w244::prelude::*;
        pub use crate::wide::w245::prelude::*;
        pub use crate::wide::w246::prelude::*;
        pub use crate::wide::w247::prelude::*;
        pub use crate::wide::w248::prelude::*;
        pub use crate::wide::w249::prelude::*;
        pub use crate::wide::w250::prelude::*;
        pub use crate::wide::w251::prelude::*;
        pub use crate::wide::w252::prelude::*;
        pub use crate::wide::w253::prelude::*;
        pub use crate::wide::w254::prelude::*;
        pub use crate::wide::w255::prelude::*;
        pub use crate::wide::w256::prelude::*;
        pub use crate::wide::w257::prelude::*;
        pub use crate::wide::w258::prelude::*;
        pub use crate::wide::w259::prelude::*;
        pub use crate::wide::mesh::*;
    }
}
pub mod shapes {
    pub struct Mesh(pub u8);
    impl Mesh {
        pub fn new(x: u8) -> Self { Mesh(x) }
    }
}
