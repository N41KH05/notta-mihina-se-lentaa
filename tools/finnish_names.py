"""Finnish names for the map: places with an established Finnish name (exonym),
Finnish transliteration of Russian and Belarusian place names, country names,
and seas/lakes. Places not listed keep their own spelling.

Keys are Natural Earth names (either the written or the plain-ASCII form).
"""

PLACES = {
    # --- Finland (bilingual towns: use the Finnish name) ---
    "Mariehamn": "Maarianhamina", "Jakobstad": "Pietarsaari", "Pietarsaari": "Pietarsaari",
    "Ekenas": "Tammisaari", "Ekenäs": "Tammisaari", "Raseborg": "Raasepori",
    "Kristinestad": "Kristiinankaupunki", "Nykarleby": "Uusikaarlepyy", "Borgå": "Porvoo",
    "Helsingfors": "Helsinki", "Åbo": "Turku", "Vasa": "Vaasa", "Karleby": "Kokkola",
    # --- Sweden ---
    "Stockholm": "Tukholma", "Luleå": "Luulaja", "Umeå": "Uumaja", "Haparanda": "Haaparanta",
    "Vannersborg": "Vänersborg", "Gothenburg": "Göteborg",
    # --- Norway ---
    "Tromsø": "Tromssa", "Tromso": "Tromssa", "Kirkenes": "Kirkkoniemi", "Vadsø": "Vesisaari",
    "Vadso": "Vesisaari", "Alta": "Alattio", "Vossavangen": "Voss",
    # --- Denmark ---
    "København": "Kööpenhamina", "Copenhagen": "Kööpenhamina", "Århus": "Aarhus",
    # --- Estonia, Latvia, Lithuania ---
    "Tallinn": "Tallinna", "Riga": "Riika", "Vilnius": "Vilna", "Liepaga": "Liepāja",
    # --- Germany, Poland ---
    "Hamburg": "Hampuri", "Lübeck": "Lyypekki", "Warsaw": "Varsova", "Munich": "München",
    "Cologne": "Köln", "Kraków": "Krakova", "Krakow": "Krakova",
    # --- Russia (Finnish names and Finnish transliteration) ---
    "Moscow": "Moskova", "St.  Petersburg": "Pietari", "St. Petersburg": "Pietari",
    "Saint Petersburg": "Pietari", "Archangel": "Arkangeli", "Arkhangelsk": "Arkangeli",
    "Petrozavodsk": "Petroskoi", "Pskov": "Pihkova", "Vyborg": "Viipuri",
    "Kandalaksha": "Kantalahti", "Kondopoga": "Kontupohja", "Kovda": "Kouta",
    "Nikel": "Nikkeli", "Svetogorsk": "Svetogorsk", "Belomorsk": "Belomorsk",
    "Velikiy Novgorod": "Novgorod", "Nizhny Novgorod": "Nižni Novgorod", "Bryansk": "Brjansk",
    "Yaroslavl": "Jaroslavl", "Ryazan": "Rjazan", "Orel": "Orjol", "Velikiye Luki": "Velikije Luki",
    "Bezhetsk": "Bežetsk", "Cherepovets": "Tšerepovets", "Gatchina": "Gattšina", "Luga": "Luuga",
    "Michurinsk": "Mitšurinsk", "Monchegorsk": "Montšegorsk", "Segezha": "Segeža",
    "Staraya Russa": "Staraja Russa", "Volkhov": "Volhov", "Vyazma": "Vjazma", "Yefremov": "Jefremov",
    "Bologoye": "Bologoje", "Borovichi": "Borovitši", "Serpukhov": "Serpuhov",
    "Solnechnogorsk": "Solnetšnogorsk", "Tikhvin": "Tihvin", "Torzhok": "Toržok",
    "Yegoryevsk": "Jegorjevsk", "Chernyakhovsk": "Tšernjahovsk", "Dyatkovo": "Djatkovo",
    "Orekhovo-Zuevo": "Orehovo-Zujevo", "Polyarnyy": "Poljarnyi", "Rzhev": "Ržev",
    "Sergiyev Posad": "Sergijev Posad", "Shchekino": "Štšokino", "Shuya": "Šuja",
    "Uglich": "Uglitš", "Vyshnniy Volochek": "Vyšni Volotšok", "Yekaterinburg": "Jekaterinburg",
    "Yakutsk": "Jakutsk", "Krasnoyarsk": "Krasnojarsk", "Khabarovsk": "Habarovsk",
    # --- Belarus, Ukraine ---
    "Vitsyebsk": "Vitebsk", "Babruysk": "Babruisk", "Hrodna": "Grodno", "Orsha": "Orša",
    "Barysaw": "Barysau", "Mahilyow": "Mogiljov", "Baranavichy": "Baranovitši",
    "Maladzyechna": "Molodetšno", "Polatsk": "Polotsk", "Kyiv": "Kiova", "Kiev": "Kiova",
    "Donetsk": "Donetsk", "Sevastopol": "Sevastopol",
    # --- Rest of Europe ---
    "London": "Lontoo", "Paris": "Pariisi", "Rome": "Rooma", "Berlin": "Berliini",
    "Athens": "Ateena", "Vienna": "Wien", "Prague": "Praha", "Brussels": "Bryssel",
    "Lisbon": "Lissabon", "Bucharest": "Bukarest", "Belgrade": "Belgrad", "Geneva": "Geneve",
    "Milan": "Milano", "Naples": "Napoli", "Seville": "Sevilla", "Nicosia": "Nikosia",
    "Thessaloniki": "Thessaloniki",
    # --- Asia, Africa, Americas ---
    "Beijing": "Peking", "Tokyo": "Tokio", "Seoul": "Soul", "Tehran": "Teheran",
    "Baghdad": "Bagdad", "Riyadh": "Riad", "Makkah": "Mekka", "Jeddah": "Jidda",
    "Damascus": "Damaskos", "Pyongyang": "Pjongjang", "Tashkent": "Taškent",
    "Ashgabat": "Ašgabat", "Bishkek": "Biškek", "Dushanbe": "Dušanbe", "Yerevan": "Jerevan",
    "Nur-Sultan": "Astana", "Ulaanbaatar": "Ulan Bator", "Kuwait City": "Kuwait",
    "Cairo": "Kairo", "Alexandria": "Aleksandria", "Algiers": "Alger", "Khartoum": "Khartum",
    "Addis Ababa": "Addis Abeba", "Cape Town": "Kapkaupunki", "Marrakesh": "Marrakech",
    "Mexico City": "México", "Havana": "Havanna", "Guatemala City": "Guatemala",
    "Panama City": "Panamá", "Washington,  D.C.": "Washington", "Washington, D.C.": "Washington",
    "Ōsaka": "Osaka", "Ürümqi": "Ürümqi",
}

# Country names: taken from the standard ISO translations (pycountry), with the
# everyday short forms preferred over official long ones.
COUNTRY_OVERRIDES = {
    "RU": "Venäjä", "GB": "Iso-Britannia", "KR": "Etelä-Korea", "KP": "Pohjois-Korea",
    "IR": "Iran", "SY": "Syyria", "LA": "Laos", "VN": "Vietnam", "TZ": "Tansania",
    "BO": "Bolivia", "VE": "Venezuela", "MD": "Moldova", "CD": "Kongon dem. tasavalta",
    "CG": "Kongo", "TW": "Taiwan", "BN": "Brunei", "PS": "Palestiina", "VA": "Vatikaani",
    "FM": "Mikronesia", "MK": "Pohjois-Makedonia", "CZ": "Tšekki", "CI": "Norsunluurannikko",
    "CV": "Kap Verde", "SZ": "Eswatini", "XK": "Kosovo", "AX": "Ahvenanmaa", "SJ": "Huippuvuoret",
    "GL": "Grönlanti", "FO": "Färsaaret", "US": "Yhdysvallat", "AE": "Arabiemiirikunnat",
    "CF": "Keski-Afrikka", "DO": "Dominikaaninen tasavalta", "BA": "Bosnia ja Hertsegovina",
}
COUNTRY_BY_ENGLISH = {             # Natural Earth entries without an ISO code
    "Kosovo": "Kosovo", "N. Cyprus": "Pohjois-Kypros", "Somaliland": "Somalimaa",
    "Aland": "Ahvenanmaa", "Northern Cyprus": "Pohjois-Kypros",
}

# Seas and big lakes: (Finnish name, English name, lat, lon, min zoom, max zoom)
WATERS = [
    ("Pohjanlahti", "Gulf of Bothnia", 62.6, 19.9, 3, 4),
    ("Merenkurkku", "Kvarken", 63.55, 20.95, 7, 11),
    ("Perämeri", "Bothnian Bay", 64.9, 23.2, 5, 9),
    ("Selkämeri", "Bothnian Sea", 62.1, 19.6, 5, 9),
    ("Ahvenanmeri", "Sea of Åland", 60.05, 19.25, 7, 10),
    ("Saaristomeri", "Archipelago Sea", 60.15, 21.55, 7, 10),
    ("Suomenlahti", "Gulf of Finland", 59.85, 25.4, 5, 9),
    ("Riianlahti", "Gulf of Riga", 57.6, 23.4, 5, 9),
    ("Itämeri", "Baltic Sea", 57.4, 19.2, 3, 8),
    ("Pohjanmeri", "North Sea", 56.5, 3.5, 3, 8),
    ("Norjanmeri", "Norwegian Sea", 68.0, 4.0, 3, 8),
    ("Barentsinmeri", "Barents Sea", 72.5, 38.0, 3, 8),
    ("Vienanmeri", "White Sea", 65.8, 36.3, 4, 9),
    ("Laatokka", "Lake Ladoga", 60.95, 31.5, 5, 10),
    ("Ääninen", "Lake Onega", 61.75, 35.4, 5, 10),
    ("Saimaa", "Saimaa", 61.25, 28.35, 7, 11),
    ("Päijänne", "Päijänne", 61.55, 25.5, 7, 11),
    ("Oulujärvi", "Oulujärvi", 64.35, 27.2, 7, 11),
    ("Inarijärvi", "Lake Inari", 69.0, 27.85, 6, 11),
    ("Pielinen", "Pielinen", 63.3, 29.65, 7, 11),
    ("Vänern", "Vänern", 58.9, 13.3, 6, 10),
    ("Peipsi", "Lake Peipus", 58.7, 27.5, 6, 10),
    ("Mustameri", "Black Sea", 43.2, 34.5, 3, 7),
    ("Kaspianmeri", "Caspian Sea", 42.0, 50.5, 3, 7),
    ("Välimeri", "Mediterranean Sea", 35.0, 18.0, 2, 6),
    ("Atlantin valtameri", "Atlantic Ocean", 40.0, -40.0, 2, 5),
    ("Tyyni valtameri", "Pacific Ocean", 10.0, -150.0, 2, 5),
    ("Intian valtameri", "Indian Ocean", -20.0, 80.0, 2, 5),
    ("Pohjoinen jäämeri", "Arctic Ocean", 81.0, 10.0, 2, 5),
]
