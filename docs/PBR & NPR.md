参数	说明	类型	默认值
Kd r g b	漫反射/基础颜色	float×3	0.8 0.8 0.8
d <value>	不透明度	float	1.0
MaterialType	覆盖当前材质类型	NPR/PBR	Default


# NPR
参数	说明	类型	默认值
LambertClamp <v>	Lambert 光照钳制值	float	0.5
RampOffset <v>	渐变偏移（选择 Ramp 贴图的行）	float	0.0
RimWidth <v>	边缘光宽度	float	0.5
RimThreshold <v>	边缘光阈值	float	0.1
RimStrength <v>	边缘光强度	float	1.0
RimColor r g b	边缘光颜色	float×3	1.0 1.0 1.0
FaceMode <0/1>	面部模式（直接输出颜色无光照）	int	0

参数	说明
map_Kd <path>	漫反射/基础颜色贴图
map_Bump <path>	法线贴图（别名 map_bump, norm）
map_Ke <path>	通道数据贴图（别名 map_lightmap, map_LightMap）<br>R=metallic, G=ao, B=specular, A=materialType
map_Ramp <path>	卡通渐变贴图（别名 map_ramp）


# PBR 

参数	说明	类型	范围	默认值
R <v> 或 Roughness <v>	粗糙度	float	0.0 ~ 1.0	0.5
M <v> 或 Metallic <v>	金属度	float	0.0 ~ 1.0	0.0
S <v> 或 Specular <v>	高光乘数（F0）	float	0.0 ~ 10.0	1.0
AlphaClip <v> 或 AlphaTest <v>	Alpha 测试阈值	float	0.0 ~ 1.0	0.0
EI<v> 或 ei <v>         自发光强度 float 0.0

PBR 纹理贴图
参数	说明	别名
map_Kd <path>	漫反射/基础颜色贴图	-
map_Bump <path>	法线贴图	map_bump, norm, map_Normal
map_ARM <path>	打包 AO/Roughness/Metallic 贴图（优先使用）	map_arm
map_Pr <path>	单独粗糙度贴图	map_Roughness, map_roughness
map_Pm <path>	单独金属度贴图	map_Metallic, map_metallic
map_AO <path>	环境光遮蔽贴图	map_Ao, map_ao, map_Ambient
map_Ke <path>	自发光贴图	map_Emission, map_emission