#include "EWSSListener.h"

#include <cstdint>
#include <string>
#include <vector>



// List of all EWSS fields and possible values.
// Should probably be moved either in app on phone or as file on filesystem.
LanguagePack messages = {
  { "TEST", "ALERT", "UPDATE", "ALL CLEAR" },
  { "Afghanistan",
    "Albania",
    "Antarctica",
    "Algeria",
    "American Samoa",
    "Andorra",
    "Angola",
    "Antigua and Barbuda",
    "Azerbaijan",
    "Argentina",
    "Australia",
    "Austria",
    "Bahamas (the)",
    "Bahrain",
    "Bangladesh",
    "Armenia",
    "Barbados",
    "Belgium",
    "Bermuda",
    "Bhutan",
    "Bolivia (Plurinational State of)",
    "Bosnia and Herzegovina",
    "Botswana",
    "Bouvet Island",
    "Brazil",
    "Belize",
    "British Indian Ocean Territory (the)",
    "Solomon Islands",
    "Virgin Islands (British)",
    "Brunei Darussalam",
    "Bulgaria",
    "Myanmar",
    "Burundi",
    "Belarus",
    "Cambodia",
    "Cameroon",
    "Canada",
    "Cabo Verde",
    "Cayman Islands (the)",
    "Central African Republic (the)",
    "Sri Lanka",
    "Chad",
    "Chile",
    "China",
    "Taiwan (Province of China)",
    "Christmas Island",
    "Cocos (Keeling) Islands (the)",
    "Colombia",
    "Comoros (the)",
    "Mayotte",
    "Congo (the)",
    "Congo (the Democratic Republic of the)",
    "Cook Islands (the)",
    "Costa Rica",
    "Croatia",
    "Cuba",
    "Cyprus",
    "Czechia",
    "Benin",
    "Denmark",
    "Dominica",
    "Dominican Republic (the)",
    "Ecuador",
    "El Salvador",
    "Equatorial Guinea",
    "Ethiopia",
    "Eritrea",
    "Estonia",
    "Faroe Islands (the)",
    "Falkland Islands (the) [Malvinas]",
    "South Georgia and the South Sandwich Islands",
    "Fiji",
    "Finland",
    "Åland Islands",
    "France",
    "French Guiana",
    "French Polynesia",
    "French Southern Territories (the)",
    "Djibouti",
    "Gabon",
    "Georgia",
    "Gambia (the)",
    "Palestine, State of",
    "Germany",
    "Ghana",
    "Gibraltar",
    "Kiribati",
    "Greece",
    "Greenland",
    "Grenada",
    "Guadeloupe",
    "Guam",
    "Guatemala",
    "Guinea",
    "Guyana",
    "Haiti",
    "Heard Island and McDonald Islands",
    "Holy See (the)",
    "Honduras",
    "Hong Kong",
    "Hungary",
    "Iceland",
    "India",
    "Indonesia",
    "Iran (Islamic Republic of)",
    "Iraq",
    "Ireland",
    "Israel",
    "Italy",
    "Côte d'Ivoire",
    "Jamaica",
    "Japan",
    "Kazakhstan",
    "Jordan",
    "Kenya",
    "Korea (the Democratic People's Republic of)",
    "Korea (the Republic of)",
    "Kuwait",
    "Kyrgyzstan",
    "Lao People's Democratic Republic (the)",
    "Lebanon",
    "Lesotho",
    "Latvia",
    "Liberia",
    "Libya",
    "Liechtenstein",
    "Lithuania",
    "Luxembourg",
    "Macao",
    "Madagascar",
    "Malawi",
    "Malaysia",
    "Maldives",
    "Mali",
    "Malta",
    "Martinique",
    "Mauritania",
    "Mauritius",
    "Mexico",
    "Monaco",
    "Mongolia",
    "Moldova (the Republic of)",
    "Montenegro",
    "Montserrat",
    "Morocco",
    "Mozambique",
    "Oman",
    "Namibia",
    "Nauru",
    "Nepal",
    "Netherlands (the)",
    "Curaçao",
    "Aruba",
    "Sint Maarten (Dutch part)",
    "Bonaire, Sint Eustatius and Saba",
    "New Caledonia",
    "Vanuatu",
    "New Zealand",
    "Nicaragua",
    "Niger (the)",
    "Nigeria",
    "Niue",
    "Norfolk Island",
    "Norway",
    "Northern Mariana Islands (the)",
    "United States Minor Outlying Islands (the)",
    "Micronesia (Federated States of)",
    "Marshall Islands (the)",
    "Palau",
    "Pakistan",
    "Panama",
    "Papua New Guinea",
    "Paraguay",
    "Peru",
    "Philippines (the)",
    "Pitcairn",
    "Poland",
    "Portugal",
    "Guinea-Bissau",
    "Timor-Leste",
    "Puerto Rico",
    "Qatar",
    "Réunion",
    "Romania",
    "Russian Federation (the)",
    "Rwanda",
    "Saint Barthélemy",
    "Saint Helena, Ascension and Tristan da Cunha",
    "Saint Kitts and Nevis",
    "Anguilla",
    "Saint Lucia",
    "Saint Martin (French part)",
    "Saint Pierre and Miquelon",
    "Saint Vincent and the Grenadines",
    "San Marino",
    "Sao Tome and Principe",
    "Saudi Arabia",
    "Senegal",
    "Serbia",
    "Seychelles",
    "Sierra Leone",
    "Singapore",
    "Slovakia",
    "Viet Nam",
    "Slovenia",
    "Somalia",
    "South Africa",
    "Zimbabwe",
    "Spain",
    "South Sudan",
    "Sudan (the)",
    "Western Sahara*",
    "Suriname",
    "Svalbard and Jan Mayen",
    "Eswatini",
    "Sweden",
    "Switzerland",
    "Syrian Arab Republic (the)",
    "Tajikistan",
    "Thailand",
    "Togo",
    "Tokelau",
    "Tonga",
    "Trinidad and Tobago",
    "United Arab Emirates (the)",
    "Tunisia",
    "Turkey",
    "Turkmenistan",
    "Turks and Caicos Islands (the)",
    "Tuvalu",
    "Uganda",
    "Ukraine",
    "North Macedonia",
    "Egypt",
    "United Kingdom of Great Britain and Northern Ireland (the)",
    "Guernsey",
    "Jersey",
    "Isle of Man",
    "Tanzania, the United Republic of",
    "United States of America (the)",
    "Virgin Islands (U.S.)",
    "Burkina Faso",
    "Uruguay",
    "Uzbekistan",
    "Venezuela (Bolivarian Republic of)",
    "Wallis and Futuna",
    "Samoa",
    "Yemen",
    "Zambia",
    "EU Organisations",
    "UN Organisations",
    "International" },
  { "CBRNE : Air strike\nAttack performed by an aircraft, usually of military type, either by bombing (or "
    "similar), firing guns or missiles.",
    "CBRNE : Attack on IT systems\nIntentional attempt to expose, alter, disable, destroy, steal or gain "
    "unauthorized access to or make unauthorised use of IT equipment.",
    "CBRNE : Attack with nuclear weapons\nFrom www.dhs.gov 'Attack performed with a device producing a "
    "nuclear explosion.",
    "CBRNE : Biological hazard\nPresence of hazardous biological materials that present a risk or potential "
    "risk to the health of humans, animals, plants, or the environment.",
    "CBRNE : Chemical hazard\nPresence of hazardous chemical materials that present a risk or potential risk "
    "to the health of humans, animals, plants, or the environment.",
    "CBRNE : Explosive hazard\nPresence of hazardous explosive materials that present a risk or potential "
    "risk to the health of humans, animals, plants, or the environment.",
    "CBRNE : Meteorite impact\nMeteorite entering from space into the earth atmosphere.",
    "CBRNE : Missile attack\nMissile launch.",
    "CBRNE : Nuclear hazard\nPresence of hazardous nuclear materials that present a risk or potential risk "
    "to the health of humans, animals, plants, or the environment.",
    "CBRNE : Nuclear power station accident\nAccident occurring at an installation where power is generated "
    "by a nuclear reactor.",
    "CBRNE : Radiological hazard\nPresence of hazardous radioactive materials that present a risk or "
    "potential risk to the health of humans, animals, plants, or the environment.",
    "CBRNE : Satellite/space re-entry debris\nSatellite or parts of a satellite or launcher or space vehicle "
    "entering or re-entering from space into the earth atmosphere.",
    "CBRNE : Siren test\nTest of a warning device emitting a loud noise.",
    "ENVIRONMENT : Acid rain\nRain containing dissolved acidifying compounds, resulting from chemical "
    "pollution of the atmosphere by sulphur and nitrogen compounds. When deposited, these increase the "
    "acidity of the soil and water causing agricultural and ecological damage.",
    "ENVIRONMENT : Air pollution\nPrecise threshold is according to each local standard.",
    "ENVIRONMENT : Contaminated drinking water\nDrinking water containing contaminants (germs and/or "
    "chemicals) above the quality standard. Precise threshold according to each local standard.",
    "ENVIRONMENT : Gas leak\nUncontrolled release of natural gas or gaseous product in the environment.",
    "ENVIRONMENT : Marine pollution\nMarine pollution occurs when substances used or spread by humans, such "
    "as industrial, agricultural and residential waste, particles, noise, excess carbon dioxide or invasive "
    "organisms enter the ocean and cause harmful effects there. Precise threshold is according to each local "
    "standard.",
    "ENVIRONMENT : Noise pollution\nDisturbing or unwanted noise that may present a potential risk to the "
    "health of humans, animals and wildlife.",
    "ENVIRONMENT : Plague of insects\nInfestation of insects or pervasive influx and development of insects "
    "or parasites affecting humans, animals, crops and materials.",
    "ENVIRONMENT : River pollution\nContamination of water in rivers, usually as a result of human "
    "activities, in such a manner that negatively affects its legitimate uses.",
    "ENVIRONMENT : Suspended dust\nMore general, particulates are microscopic solid particles suspended in "
    "the air. The source of the particulate matter can be natural or anthropogenic having adverse effect on "
    "human health. The IARC and WHO designate airborne particulates as a Group 1 carcinogen. Particulates "
    "are the most harmful form of air pollution due to their ability to penetrate deep into the lungs, blood "
    "streams and brain, causing health problems including heart attacks, respiratory disease, and premature "
    "death.",
    "ENVIRONMENT : UV radiation\nPrecise threshold is according to each local standard.",
    "FIRE : Conflagration\nA large and destructive fire that threatens human life, health, and/or property.",
    "FIRE : Fire brigade deployment\nOperation of firefighters in response to a fire or other emergency "
    "report. Note: Definition may vary, depending on the responsibility of the fire brigade in each country",
    "FIRE : Fire gases\nPotentially dangerous gases emitted from a fire.",
    "FIRE : Forest fire\nA type of wildfire in a wooded area.",
    "FIRE : Fumes\nAn often-noxious suspension of particles in the air.",
    "FIRE : Odour nuisance\nProlonged and offensive smell leading to the impairment of human well-being over "
    "a certain period of time at regular or irregular intervals.",
    "FIRE : Risk of fire\nIncreased likelihood of accidental fire.",
    "FIRE : Structure fire / Industrial fire Fire affecting the structural parts of a building or a fire "
    "affecting an industrial site.",
    "GEO : Ash fall\nFine (less than 4 mm in diameter) unconsolidated volcanic debris blown into the "
    "atmosphere during an eruption can remain airborne for long periods of time and travel considerable "
    "distance from the source.",
    "GEO : Avalanche risk\nA large mass of loosened earth material, snow, or ice that slides, flows or falls "
    "rapidly down a mountainside under the force of gravity. Snow Avalanche: Rapid downslope movement of a "
    "mix of snow and ice. Debris Avalanche: The sudden and very rapid downslope movement of unsorted mass of "
    "rock and soil. There are two general types of debris avalanchesa cold debris avalanche usually results "
    "from an unstable slope suddenly collapsing whereas a hot debris avalanche results from volcanic "
    "activity leading to slope instability and collapse.",
    "GEO : Crack in the ground/sinkhole\nCollapse of the land surface due to the dissolving of the "
    "subsurface rocks such as limestone or carbonate rock by water.",
    "GEO : Debris flow\nTypes of landslides that occur when heavy rain or rapid snow/ice melt send large "
    "amounts of vegetation, mud, or rock downslope by gravitational forces. Includes mud flow, rock fall.",
    "GEO : Earthquake Sudden movement of a block of the Earth’s crust along a geological fault and "
    "associated ground shaking.",
    "GEO : Geomagnetic or solar storm\nA type of extra-terrestrial hazard caused by solar wind shockwaves "
    "that temporarily disturb the Earth’s magnetosphere. Geomagnetic storms can disrupt power grids, "
    "spacecraft operations, and satellite communications.",
    "GEO : Glacial ice avalanche\nIce calving at end of a glacier.",
    "GEO : Landslide Independent of the presence of water, mass movement may also be triggered by "
    "earthquakes.",
    "GEO : Lava flow\nThe ejected magma that moves as a liquid mass downslope from a volcano during an "
    "eruption.",
    "GEO : Pyroclastic flow\nExtremely hot gases, ash, and other materials of more than 1,000 degrees "
    "Celsius that rapidly flow down the flank of a volcano (more than 700 km/h) during an eruption.",
    "GEO : Snowdrifts\nMass of snow heaped up by the wind and deposited along an obstruction or an "
    "irregularity of the terrain.",
    "GEO : Tidal wave\nShallow water wave caused by the gravitational interactions between the Sun, Moon, "
    "and Earth. (from USGS)",
    "GEO : Tsunami\nA series of waves (with long wavelengths when traveling across the deep ocean) that are "
    "generated by a displacement of massive amounts of water through underwater earthquakes, volcanic "
    "eruptions or landslides. Tsunami waves travel at very high speed across the ocean but as they begin to "
    "reach shallow water they slow down and the wave grows steeper.",
    "GEO : Volcanic mud flow\n(Also known as Lahar) A mud flow resulting from a volcanic event. Hot or cold "
    "mixture of earthen material flowing on the slope of a volcano either during or between volcanic "
    "eruptions.",
    "GEO : Volcano eruption\nA type of volcanic event near an opening/vent in the Earth’s surface including "
    "volcanic eruptions of lava, ash, hot vapour, gas, and pyroclastic material.",
    "GEO : Wind/wave/storm surge\nAn abnormal rise in sea level generated by a tropical cyclone or other "
    "intense storms.",
    "HEALTH : Epizootic\nAn epidemic outbreak of disease in an animal population, often with the implication "
    "that it may extend to humans.",
    "HEALTH : Food safety alert\nPresence of unsafe food containing harmful bacteria, viruses, parasites or "
    "chemical substances that present a risk to the health of humans, animals, plants or the environment.",
    "HEALTH : Health hazard\nPotential source of danger for individual's health.",
    "HEALTH : Pandemi\nAn epidemic of an infectious disease that has spread across a large region, for "
    "instance multiple continents or worldwide, affecting a substantial number of people. A widespread "
    "endemic disease with a stable number of infected people is not a pandemic. Widespread endemic diseases "
    "with a stable number of infected people such as recurrences of seasonal influenza are generally "
    "excluded as they occur simultaneously in large regions of the globe rather than being spread worldwide.",
    "HEALTH : Pest infestation\n(from GEMET )1) The occurrence of one or more pest species in an area or "
    "location where their numbers and impact are currently or potentially at intolerable levels. 2) A sudden "
    "increase in destructiveness or population numbers of a pest species in a given area.",
    "HEALTH : Risk of infection\nPresent of infectious agent(s) that present a risk or potential risk to the "
    "health of humans, animals, plants, or the environment.",
    "INFRASTRUCTURE : Building collapse\nSudden failure of the building structure resulting in its partial "
    "or total destruction.",
    "INFRASTRUCTURE : Emergency number outage\nInterruption or failure of emergency number service.",
    "INFRASTRUCTURE : Gas supply outage\nInterruption or failure of gas supply service.",
    "INFRASTRUCTURE : Outage of IT systems\nInterruption or failure of IT systems services.",
    "INFRASTRUCTURE : Power outage\nInterruption or failure of electricity distribution service.",
    "INFRASTRUCTURE : Raw sewage\nRelease in the environment of untreated wastewater.",
    "INFRASTRUCTURE : Telephone line outage\nInterruption or failure of telephone service.",
    "MET : Black Ice\nWMO definition: (1) Thin, new ice on freshwater or saltwater, appearing dark in colour "
    "because of its transparency, which is a result of its columnar grain structure. On lakes, black ice is "
    "commonly overlain by white ice formed from refrozen snow or slush. (2) A popular alternative for glaze. "
    "A thin sheet of ice, relatively dark in appearance, may form when light rain or drizzle falls on a road "
    "surface that is at a temperature below 0°C or, alternatively, when water already on the road surface "
    "subsequently freezes when the temperature thereof falls below freezing point. It may also be formed "
    "when supercooled fog droplets are intercepted by buildings, fences and vegetation.",
    "MET : Coastal flooding\nOccurs when dry and low-lying land is submerged by seawater.",
    "MET : Cold wave\nExtreme cold and low temperatures.",
    "MET : Derecho\nWidespread and usually fast-moving windstorms associated with convection/convective "
    "storm. Derechos include downburst and straight-line winds. The damage from derechos is often confused "
    "with the damage from tornadoes.",
    "MET : Drought\nAn extended period of unusually low precipitation that produces a shortage of water for "
    "people, animals and plants. Drought is different from most other hazards in that it develops slowly, "
    "sometimes even over years, and its onset is generally difficult to detect.",
    "MET : Dust storm\nStrong winds carry particles of sand aloft, but generally confined to less than 50 "
    "feet (15 m), especially common in arid and semi-arid environments. A dust storm is also characterised "
    "by strong winds but carries smaller particles of dust rather than sand over an extensive area.",
    "MET : Floating ice / icebergs\nFloating ice in rivers or icebergs in the open sea",
    "MET : Flood\nA general term for the overflow of water from a stream channel onto normally dry land in "
    "the floodplain (riverine flooding), higher-than normal levels along the coast and in lakes or "
    "reservoirs (coastal flooding) as well as ponding of water at or near the point where the rain fell "
    "(flash floods).",
    "MET : Fog A suspension of very small, usually microscopic water droplets in the air, reducing "
    "visibility at the Earth’s surface.",
    "MET : Hail Solid precipitation in the form of irregular pellets or balls of ice more than 5 mm in "
    "diameter.",
    "MET : Heat wave\nA general term for temperature variations above (extreme heat).",
    "MET : Lightning\nA high-voltage, visible electrical discharge produced by a thunderstorm and followed "
    "by the sound of thunder.",
    "MET : Pollens\nPollens that can cause human allergies are those of anemophilous plants (pollen is "
    "dispersed by air currents.) Such plants produce large quantities of lightweight pollen (because wind "
    "dispersal is random and the likelihood of one pollen grain landing on another flower is small), which "
    "can be carried for great distances and are easily inhaled by humans.",
    "MET : Rainfall Rainfall greater than or equal to 50mm in past 24 hours. Note: Precise threshold is "
    "according to each local standard.",
    "MET : Snow storm / blizzard\nA low-pressure system in winter months with significant accumulations of "
    "snow, freezing rain, sleet or ice. A blizzard is a severe snowstorm with winds exceeding 35 mph (56 "
    "km/h) for three or more hours, producing reduced visibility (less than .25 mile (400 m).",
    "MET : Snowfall\nSnowfall greater than or equal to 50mm in past 24 hours. Note: Precise threshold is "
    "according to each local standard.",
    "MET : Storm or thunderstorm\nMeteorological event generating winds equal or higher than 10 on the "
    "Beaufort scale.",
    "MET : Thawing\nGradual warning causing ground ice to degrade, potentially leading the soil surface to "
    "collapse.",
    "MET : Tornado\nA violently rotating column of air that reaches the ground or open water (waterspout).",
    "MET : Tropical cyclone (hurricane)\nA tropical cyclone originates over tropical or subtropical waters. "
    "It is characterised by a warm-core, non-frontal synoptic-scale cyclone with a low-pressure centre, "
    "spiral rain bands and strong winds. Depending on their location, tropical cyclones are referred to as "
    "hurricanes (Atlantic, Northeast Pacific), typhoons (Northwest Pacific), or cyclones (South Pacific and "
    "Indian Ocean). With a minimum wind speed of 119km/h (or 74 miles/h) a tropical storm in the Atlantic or "
    "Northeast Pacific is called a hurricane.",
    "MET : Wind chill/frost\nFrost is the consequence of radiative cooling resulting in the formation of "
    "thin ice crystals on the ground or other surfaces in the form of needles, feathers, scales, or fans. "
    "Frost occurs when the temperature of surfaces is below freezing and water vapor from humid air forms "
    "solid deposits on the cold surface. Freeze occurs when the air temperature is at (32˚F/0˚C) or below "
    "over a widespread area for a climatologically significant period of time. Use of the term is usually "
    "restricted to advective situations or to occasions when wind or other conditions prevent frost. Frost "
    "and freeze are particularly damaging during the crop growing season.",
    "MET : Tropical cyclone (typhoon)\nA tropical cyclone originates over tropical or subtropical waters. It "
    "is characterised by a warm-core, non-frontal synoptic-scale cyclone with a low-pressure centre, spiral "
    "rain bands and strong winds. Depending on their location, tropical cyclones are referred to as "
    "hurricanes (Atlantic, Northeast Pacific), typhoons (Northwest Pacific), or cyclones (South Pacific and "
    "Indian Ocean). With a minimum wind speed of 119km/h (or 74 miles/h) a tropical storm in the Atlantic or "
    "Northeast Pacific is called a hurricane.",
    "RESCUE : Dam failure or bursting of a dam\nIncident at a dam potentially leading to the uncontrolled "
    "release of the dam content",
    "RESCUE : Dike failure or bursting of a dike\nIncident at a dike potentially leading to the uncontrolled "
    "release of water",
    "RESCUE : Explosive ordnance disposal\nActivity to render safe all types of ordnance, including "
    "explosive, chemical and biological. It covers land and underwater location, identification, "
    "render-safe, and recovery (or disposal) of ordnance.",
    "RESCUE : Factory accident\nIncident at an industrial site affecting the surrounding area",
    "RESCUE : Mine hazard\n Any of the dangers peculiar to the winning and working of coal and minerals. "
    "These include collapse of ground, explosion of released gas, inundation by water, spontaneous "
    "combustion, inhalation of dust and poisonous gases, etc.",
    "SAFETY : Bomb/ammunition discovery\nUnexpected presence of bomb or ammunition",
    "SAFETY : Demonstration\nA march or gathering of people taking part in to show their opposition to "
    "something or their support for something",
    "SAFETY : Hazardous material accident\nAn unexpected uncontrolled release of substances that have the "
    "potential to harm a person or the environment upon contact.",
    "SAFETY : Life Threatening situation\nEmergency situation that requires immediate intervention to save "
    "the life of the persons concerned.",
    "SAFETY : Major event\nPlanned happening which may generate significant disruption in road traffic or "
    "transport",
    "SAFETY : Missing person/abduction\nUnaccounted person whose health status (alive/dead) cannot be "
    "confirmed.",
    "SAFETY : Risk of explosion\nIntentional use of explosive materials aiming at killing or damaging the "
    "health of humans, animals, plants, or the environment.",
    "SAFETY : Safety warning\nAnnouncement of a potentially dangerous situation or event to the public "
    "impacting their safety.",
    "SAFETY : Undefined flying object\nPresence of an unidentified flying object which may present a risk or "
    "potential risk to the health of humans, animals, plants, or the environment. Hurricanes and typhoons "
    "are the same weather phenomenon. A tropical cyclone is a generic term used to describe a rotating, "
    "organized system of clouds and thunderstorms that originates over tropical or subtropical waters and "
    "has closed, low-level circulation. The classification as a hurricane, typhoon or tropical cyclone "
    "depends upon where the storm originates in the world. Although, in this list of hazards hurricanes and "
    "typhoons are identified separately.",
    "SAFETY : Unidentified animal\nPresence of an unidentified animal which may present a risk or a "
    "potential risk to the health of humans, animals, plants, or the environment.",
    "SECURITY : Chemical attack\nIntentional use of dangerous chemical materials aiming at killing or "
    "damaging the health of humans, animals, plants, or the environment.",
    "SECURITY : Guerrilla attack\nParamilitary armed forces carrying violent actions.",
    "SECURITY : Hijack\nAirplane or other vehicle under the control of hijackers",
    "SECURITY : Shooting or danger due to weapons\nDangerous use of firearm(s) or weapons in a populated "
    "area.",
    "SECURITY : Special forces attack\nMilitary armed forces carrying violent actions.",
    "SECURITY : Terrorism\nTerrorist action aiming at generating a high number of victims or major material "
    "damage.",
    "TRANSPORT : Aircraft crash\nAircraft crash",
    "TRANSPORT : Bridge collapse\nStructural failure of a bridge resulting in its partial or total "
    "destruction.",
    "TRANSPORT : Dangerous goods accident\nAccident occurrence associated with and related to the conveyance "
    "of dangerous goods by air, road, inland waterways and sea, which results in fatal or serious injury to "
    "a person or major damage to property or environment.",
    "TRANSPORT : Inland waterway transport accident\nShip accident on an inland waterway.",
    "TRANSPORT : Nautical disaster/Maritime/Marine Security\nSinking ship or any other event occurring at "
    "sea which may present of security or safety risk.",
    "TRANSPORT : Oil spill\nLeak of oil or pollutant from a ship or other transportation mean resulting in "
    "pollution.",
    "TRANSPORT : Road traffic incident\nAnnouncement of road traffic disruption.",
    "TRANSPORT : Train/rail accident\nAccident occurring on a railway and involving train(s) and/or other "
    "vehicles.",
    "TRANSPORT : Tunnel accident\nCar crash or railway accident occurring in a tunnel.",
    "OTHER : Test alert\nTest alert message for end-to-end testing." },
  { "Unknown", "Moderate : Possible threat to life or property",
    "Severe : Significant threat to life or property", "Extreme : Extraordinary threat to life or property" },
  { "Current", "Next" },
  { "Duration unknown", "Duration < 6", "6 <= Duration < 12", "12 <= Duration < 24" },
  { "",
    "You are in the danger zone, leave the area immediately. Listen to radio or media for directions and "
    "information.",
    "You are in the danger zone, leave the area immediately and reach the evacuation point indicated by the "
    "area plotted in yellow. Listen to radio or media for directions and information.",
    "Seek shelter in a building immediately. Stay under cover and stay informed.",
    "Seek out a cellar or interior rooms on lower floors.",
    "If you are in an alpine terrain, start descending immediately and seek for shelter.",
    "Quickly move into interior rooms. If you are in a vehicle: Stop driving immediately on the edge of the "
    "road. If a building is nearby, seek shelter in that building.",
    "If you are in open terrain and you cannot find shelter, lie face-down on the ground and protect your "
    "head and neck with your hands, in a hollow where possible",
    "Prepare for evacuation. Take only the essentials with you, especially ID cards, passport, credit cards "
    "and cash. Evacuate only after the instruction of the emergency authorities.",
    "Prepare emergency food and relief material: Check and restock your equipment and supplies of water, "
    "food, medicine, cash and batteries.",
    "Stay away from glass surfaces such as windows and glass doors. There is a risk of injury from glass "
    "splinters.",
    "Reduce your power consumption to a minimum.",
    "Reduce your water consumption to a minimum.",
    "Boil water before drinking it or using it in the kitchen.",
    "Keep at least one metre away from any conversation partners. Avoid physical contact with other people "
    "such as kissing and shaking hands. Wash your hands regularly and thoroughly.",
    "Do not drink any tap water. Avoid any skin contact with tap water. Only drink mineral water from a "
    "bottle. Turn off the water supply to your house.",
    "Watch out for escaping gas. This can be indicated by hissing noises or a typical gas odour. Do not use "
    "matches, lighters or the like: naked flames in combination with leaking gas can lead to explosions and "
    "fires.",
    "Do not go outside and do not use your car.",
    "Do not touch any objects that seem suspicious to you. Debris can cause additional hazards such as fires "
    "and explosions. Inform the emergency services about damage and debris.",
    "Do not enter smoke-filled rooms. Deadly gases can form there.",
    "Do not enter cellars or underground car parks.",
    "Do not leave pets or livestock outside.",
    "Do not touch any dead animals. Report any findings of dead wild animals to the authorities.",
    "Avoid driving",
    "Avoid all items with metal parts such as umbrellas and bicycles. Do not bathe or shower during a "
    "thunderstorm. Bathing and showering can be life-threatening.",
    "Avoid rooms directly underneath the roof truss. Avoid very large rooms, such as halls, in which the "
    "ceiling is not supported by pillars.",
    "Avoid going outdoors. Keep away from trees, towers and masts. Keep at least 20",
    "Avoid the danger area",
    "Avoid going out when it is not necessary",
    "This is only a test. You do not have to take any action or to adopt any particular sheltering behaviour",
    "This replaces the warning previously in effect for this area.",
    "Conditions have improved and are no longer expected to meet alert criteria." },
  { "",
    "Check with the weather services and local authorities for additional information",
    "Find out the location of the information points set up by the authorities on official channels (radio, "
    "internet, TV, social networks…)",
    "Sensitive or vulnerable people should not go out unless they must.",
    "Rescue operation under process by security forces and emergency services. Avoid moving to facilitate "
    "security and emergency actions.",
    "Protect the most vulnerable and hear from your loved ones. Be aware of their special needs and support, "
    "as required. If you notice distressed or vulnerable persons, call the emergency services. Provide first "
    "aid if necessary but do not put yourself in any danger.",
    "Pay attention to announcements made by the police, fire brigade and by officials.",
    "Stay aware, keep listening to official instructions broadcast on the radio, television, websites and "
    "social networks pages",
    "If you need help leaving your home, call the emergency services.",
    "Only make phone calls in serious emergencies to avoid overloading the mobile network.",
    "Extreme intensity weather phenomena expected. The weather is very dangerous and implies high level of "
    "threat to health, even the life hazard. BE AWARE and keep up to date with the latest weather forecast.",
    "Severe weather expected. BE PREPARED. Take precautions and keep up to date with the latest weather "
    "forecast. Severe damages to people and properties may occur, especially to those vulnerable or in "
    "exposed areas.",
    "Moderate intensity weather phenomena expected. BE AWARE, keep up to date with the latest weather "
    "forecast. Moderate damages to people and properties may occur, especially to those vulnerable or in "
    "exposed areas",
    "BE PREPARED to protect yourself and your property. Flooding of properties and transport networks is "
    "expected. Disruption to power, communications and water supplies are possible. Evacuation may be "
    "required. Dangerous driving conditions due to reduced visibility and aquaplaning",
    "Do not go near or in flooded waters. Do not walk or drive on a submerged road. Flood waves may surprise "
    "you, the river bank may collapse or you could be sucked in a manhole or hit by a floating debris. Keep "
    "drains and shafts clear so that the water can drain away. Secure and/or move assets away from "
    "vulnerable area (car along the river, basements).",
    "Take shelter in the most resistant part of a permanent building, a municipal shelter if possible, and "
    "keep away from windows. BE AWARE of the “eye of the storm”, the calm area in its centre. It will be "
    "followed by an inversion and the strengthening of winds. Do not go outside and do not use your car. "
    "Wait until the alert is over.",
    "TAKE PRECAUTIONS, High temperatures are expected. Protect yourself from the heat and avoid physical and "
    "sports activities. Wet your body several times a day. Drink plenty of water and eat light food.",
    "Forest fire danger. Under these conditions fires may develop and spread rapidly resulting in damage to "
    "property and possible loss of human and/or animal life. Do not throw away any burning cigarettes or "
    "matches to the environment. Do not make a fire outdoors. Do not light any fireworks. Do not barbeque in "
    "open places. Vegetation is easily ignited and large areas may be affected. Follow the instructions from "
    "the local authorities.",
    "Risks of fire. Use permanent fireplaces when barbecuing. Make sure your fire is completely extinguished "
    "before you leave. Only light fireworks with the permission of the municipality, keep a safe distance "
    "from the forest and have water to hand.",
    "Keep as far away as possible from coastal areas, beaches and rivers. Get immediately to the highest "
    "ground possible and wait until the alert is over. If you are in danger of being overtaken by waves, "
    "climb onto a roof or up a solid tree, or cling on to a floating object carried along by the water.",
    "Do not go to sea and keep as far away as possible from the coast and wait until the alert is over. If "
    "you are at sea, don’t return to port. Keep away from the coast. Waves are much less dangerous out at "
    "sea.",
    "Leave the affected area immediately and seek higher ground or move to higher parts of the building. "
    "Listen to radio or media for directions and information",
    "Indoors: during the quake, take shelter near a wall or a solid piece of furniture. Outside: during the "
    "quake, keep away from anything that might collapse. In a car: during the quake, stop as far away from "
    "buildings as you can. After, be prepared for aftershocks. If you are indoor, leave by the stairs.",
    "Leave the impact site immediately and cover your mouth and nose with improvised respiratory protection "
    "(cloth, garment, surgical mask). This protects you from dust, but not from gaseous hazardous "
    "substances. Seek out a building. Move wherever possible at a right angle to the wind direction as this "
    "is the quickest way to leave the danger zone with a possible cloud of hazardous substances.",
    "Switch off the ventilation and air conditioning systems. Close all windows, doors and shutters. Cover "
    "your mouth and nose and breathe through a facemask or an improvised respiratory protection (cloth, "
    "garment, surgical mask) if the air is filled with smoke and ashes",
    "Have iodine tablets ready. DO NOT take the iodine tablets now. If this becomes necessary, we will "
    "inform you in good time.",
    "Take the iodine tablets NOW according to the package insert.",
    "Avoid watering your plants during the hottest hours, avoid using water for secondary uses such as "
    "washing your car.",
    "Seek shelter if you cannot leave the area immediately.",
    "reserved",
    "reserved",
    "This replaces the warning previously in effect for this area" },
  { "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY", "FRIDAY", "SATURDAY", "SUNDAY" },
  { "am", "pm" }
};

/*
Reads `amount` bits from `data` starting from offset.

Writes resulting bits in outbuffer, first byte might contain padding at beginning

Both amount and offset are in bits, i.e. an offset of 8 means starting from `data[1]`
*/
static uint8_t readBits(uint8_t *outbuffer, const uint8_t *data, uint8_t offset, uint8_t amount) {
  const uint8_t *cursor = data + (offset / 8);
  // First byte containing needed data

  // Clearing buffer before starting
  uint8_t usedCells = (amount / 8) + (amount % 8 != 0 ? 1 : 0);
  for (uint8_t i = 0; i < usedCells; i++) {
    outbuffer[i] = 0;
  }

  uint8_t readPos = 7 - (offset % 8);
  uint8_t shiftsRemaining = ((amount - 1) % 8);

  for (uint8_t i = 0; i < amount; i++) {

    outbuffer[0] |= ((cursor[0] & (1 << readPos)) >> readPos);

    if (shiftsRemaining == 0) {
      outbuffer++;
    } else {
      outbuffer[0] <<= 1;
    }
    shiftsRemaining = (shiftsRemaining - 1) % 8;

    if (readPos == 0) {
      cursor++;
    }

    readPos = (readPos - 1) % 8;
  }
  return amount;
}

void EWSSListener::onChannelDataRecv(const mesh::GroupChannel &channel, mesh::Packet *pkt, uint16_t data_type,
                                     const uint8_t *data, size_t data_len) {
  MyMesh::onChannelDataRecv(channel, pkt, data_type, data, data_len);

  if (data_type == EWSS_CHANNEL) {
    parseEWSS(data, data_len);
  }
}

/*
Parses payload and prints EWM on console.

Detail of each field can be found in the Common Alert Message format specification at
https://www.gsc-europa.eu/sites/default/files/sites/all/files/EWSS-CAMF_in_force.pdf
*/

struct EWM EWSSListener::parseEWSS(const uint8_t *data, size_t len) {

  EWM message;
  uint8_t buffer[3];
  uint8_t cursor = 0;

  cursor += readBits(buffer, data, cursor, 3);
  message.type = buffer[0];

  cursor += readBits(buffer, data, cursor, 9);
  message.region = (data[0] << 8) + data[1];

  cursor += 5; // Provider ID, not handled for now

  cursor += readBits(buffer, data, cursor, 7);
  message.hazard = buffer[0];

  cursor += readBits(buffer, data, cursor, 2);
  message.severity = buffer[0];

  cursor += readBits(buffer, data, cursor, 1);
  message.current_week = buffer[0];

  cursor += 14; // Time of event, not handling for now.

  cursor += readBits(buffer, data, cursor, 2);
  message.duration = buffer[0];

  cursor += 1; // Type of library
  cursor += 3; // Version of lib.

  cursor += readBits(buffer, data, cursor, 5);
  message.instrA = buffer[0];

  cursor += readBits(buffer, data, cursor, 5);
  message.instrB = buffer[0];

  cursor += 16; // Ellipse centre latitude
  cursor += 17; // Ellipse centre longitude

  cursor += 5; // Semi-major axis
  cursor += 5; // Semi-minor axis

  cursor += 6; // Azimuth

  cursor += 2;  // Type of additionnal setting
  cursor += 15; // Additionnal setting parameter
}

EWSSListener::EWSSListener(mesh::Radio &radio, mesh::RNG &rng, mesh::RTCClock &rtc, SimpleMeshTables &tables,
                           DataStore &store, AbstractUITask *ui)
    : MyMesh(radio, rng, rtc, tables, store, ui) {};