ModelThumbs - STL ve 3MF için Windows Gezgini küçük resim eklentisi
====================================================================

Ne yapar?
  Dosya Gezgini'nde .stl ve .3mf dosyalarının simgesi yerine modelin küçük
  resmini gösterir. Klasörü "Büyük simgeler" veya "Orta simgeler" görünümüne
  alınca resimleri görürsün.

  - 3MF (Elegoo/Orca/Bambu slicer projeleri): slicer'ın dosyanın içine
    gömdüğü renkli plaka önizlemelerini kullanır. Birden çok plaka varsa
    hepsi tek resimde birleştirilir.
  - 3MF (önizlemesi olmayan, CAD çıktısı vb.) ve STL: modelin geometrisini
    okuyup kendisi çizer (mavi tonlu, tek açıdan).

Gereksinimler
  Windows 10 veya 11, 64 bit.

KURULUM
  1) Zip'i bir klasöre çıkar (ModelThumbs.dll, install.bat, uninstall.bat).
  2) install.bat dosyasına çift tıkla. Yönetici izni ister, onayla.
  3) Sonda "Explorer yeniden başlatılsın mı" diye sorar. E dersen eski
     küçük resim önbelleği silinir ve Explorer yeniden açılır (açık klasör
     pencereleri kapanır). Bu adım yeni resimlerin hemen görünmesi içindir.

  Yönetici izni vermek istemezsen sadece kendi hesabın için kurulum:
     regsvr32 /n /i:user "C:\tam\yol\ModelThumbs.dll"
  (DLL'i kalıcı bir klasöre koy; sonradan taşırsan yeniden kur.)

KALDIRMA
  uninstall.bat dosyasını çalıştır.

ÖNCE DENEMEK İSTERSEN (kurulum gerekmez)
  thumbtest.exe, eklentideki aynı kodla tek bir dosyadan küçük resim üretip
  BMP olarak kaydeder:
     thumbtest.exe "C:\yol\Stapelspiel2komplett.3mf" cikti.bmp 256
  Opsiyonlar: boyut (varsayılan 256), --geometry (slicer önizlemesini yok
  say, geometriyi çiz).
  Bu çalışıyorsa ama Gezgin'de görünmüyorsa sorun kayıt/önbellek tarafındadır
  (aşağıya bak).

SORUN GİDERME
  - Resim görünmüyor, hâlâ eski simge: önbellek. install.bat'taki
    "Explorer yeniden başlat" adımını E ile çalıştır. Olmazsa:
    Başlat > "Disk Temizleme" > "Küçük resimler" seç > Temizle.
  - Gezgin Seçenekleri > Görünüm > "Her zaman simgeleri göster, küçük
    resimleri hiçbir zaman gösterme" KAPALI olmalı.
  - Klasör görünümü "Liste" veya "Ayrıntılar" ise küçük resim görünmez;
    "Büyük simgeler" seç.
  - .3mf için başka bir program (ör. 3D Viewer, slicer) kendi küçük resim
    sağlayıcısını kaydetmiş olabilir ve Windows onu önceliklendirebilir.
    Bu durumda bu eklenti o dosya türünde devreye girmeyebilir.
  - Çok büyük dosyalar (yüzlerce MB) için Gezgin zaman aşımına uğrayıp
    simge gösterebilir.

NOTLAR VE SINIRLAR
  - Eklenti imzasız (sertifikasız) bir DLL'dir. Windows/antivirüs uyarı
    verebilir. Kaynak kodu zip içindeki src klasöründedir; kendin
    inceleyip derleyebilirsin (bkz. build_windows.sh).
  - Eklenti Gezgin tarafında ayrı bir işlemde (dllhost.exe) çalışır; bozuk
    bir dosya Gezgin'in kendisini çökertmez. 68 bozuk/kesik dosya ile bellek
    denetleyicisi altında test edildi, çökme olmadı.
  - Geometri çizimi basit bir yazılım çizicidir: tek renk, tek bakış
    açısı, 3MF'teki filament renkleri yok. Z ekseni yukarı varsayılır.
  - 3MF geometri yedeği build dönüşümlerini ve bileşenleri (Bambu/Orca
    tarzı harici model dosyaları dahil) uygular; farklı malzeme/doku
    bilgisi okunmaz.
  - Bu sürüm yalnızca 64-bit Windows içindir (ARM64 için ayrı derleme gerekir).
  - Bu eklenti Windows üzerinde gerçek Gezgin ile test edilmeden teslim
    edilmiştir: çekirdek (zip/PNG/STL/3MF/çizim) Linux'ta gerçek dosyalarla
    doğrulandı, Windows DLL'i derlenip yapısı (x64, dışa açılan fonksiyonlar,
    bağımlılıklar) kontrol edildi. İlk kurulumda sorun çıkarsa bildir.

Kaynak kod
  src\core.hpp      zip/deflate, PNG, STL, 3MF, kontak sayfası, çizici
  src\provider.cpp  Gezgin (COM) küçük resim sağlayıcısı ve kayıt kodu
  src\test_core.cpp thumbtest.exe aracı
  build_windows.sh  zig ile Linux/macOS'ta Windows'a çapraz derleme
                    (pip install ziglang). Windows'ta: MinGW-w64 veya
                    Visual Studio ile src\provider.cpp derlenebilir.
