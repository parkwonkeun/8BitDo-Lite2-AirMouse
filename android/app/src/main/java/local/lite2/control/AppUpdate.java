package local.lite2.control;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.DownloadManager;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.provider.Settings;
import android.widget.TextView;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.net.URL;
import java.security.MessageDigest;
import java.util.Arrays;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import javax.net.ssl.HttpsURLConnection;

public final class AppUpdate extends Activity {
    private static final String REPO="parkwonkeun/8BitDo-Lite2-AirMouse";
    private static final Pattern APK=Pattern.compile("Lite2Control-(\\d+\\.\\d+\\.\\d+)\\.apk");
    private final Handler main=new Handler();
    private SharedPreferences pending;
    private DownloadManager downloads;
    private TextView status;
    private String installed;
    private int language;
    private boolean waitingForPermission;
    private boolean verifying;
    private boolean installing;

    private String t(String s,Object... args){return Words.get(language,s,args);}

    @Override public void onCreate(Bundle state){
        super.onCreate(state);
        language=getIntent().getIntExtra("language",3);
        setTitle(t("앱 업데이트"));
        status=new TextView(this);
        int pad=(int)(24*getResources().getDisplayMetrics().density);
        status.setPadding(pad,pad,pad,pad);
        status.setTextSize(18);
        setContentView(status);
        pending=getSharedPreferences("appUpdate",MODE_PRIVATE);
        downloads=getSystemService(DownloadManager.class);
        installing=state!=null && state.getBoolean("installing");
        waitingForPermission=state!=null && state.getBoolean("permission");
        try {
            installed=getPackageManager().getPackageInfo(getPackageName(),0).versionName;
            if(installed.equals(pending.getString("version","")))clearPending();
            if(pending.contains("id"))poll();else checkRelease();
        } catch(Exception e){fail(e);}
    }
    @Override public void onSaveInstanceState(Bundle out){
        super.onSaveInstanceState(out);
        out.putBoolean("permission",waitingForPermission);
        out.putBoolean("installing",installing);
    }
    @Override public void onDestroy(){main.removeCallbacksAndMessages(null);super.onDestroy();}
    private boolean live(){return !isFinishing() && !isDestroyed();}
    private void checkRelease(){
        status.setText(t("업데이트 확인 중…"));
        new Thread(()->{
            try {
                HttpsURLConnection conn=(HttpsURLConnection)new URL("https://api.github.com/repos/"+REPO+"/releases?per_page=100").openConnection();
                conn.setConnectTimeout(12000);
                conn.setReadTimeout(20000);
                conn.setRequestProperty("Accept","application/vnd.github+json");
                conn.setRequestProperty("User-Agent","Lite2Control/"+installed);
                JSONArray releases;
                try(InputStream in=conn.getInputStream();ByteArrayOutputStream bytes=new ByteArrayOutputStream()){
                    byte[] buf=new byte[8192];
                    int n;
                    while((n=in.read(buf))!=-1)bytes.write(buf,0,n);
                    releases=new JSONArray(bytes.toString("UTF-8"));
                } finally {conn.disconnect();}
                JSONObject best=null;
                String bestVersion="0.0.0";
                for(int i=0;i<releases.length();i++){
                    JSONObject release=releases.getJSONObject(i);
                    if(release.optBoolean("draft") || release.optBoolean("prerelease"))continue;
                    JSONArray assets=release.getJSONArray("assets");
                    for(int j=0;j<assets.length();j++){
                        JSONObject asset=assets.getJSONObject(j);
                        Matcher match=APK.matcher(asset.getString("name"));
                        if(match.matches() && compare(match.group(1),bestVersion)>0){
                            best=asset;
                            bestVersion=match.group(1);
                        }
                    }
                }
                if(best==null)throw new Exception("GitHub APK");
                final JSONObject asset=best;
                final String version=bestVersion;
                runOnUiThread(()->{
                    if(!live())return;
                    if(compare(version,installed)<=0)status.setText(t("설치된 버전 %s · GitHub 버전 %s",installed,version));
                    else startDownload(asset,version);
                });
            } catch(Exception e){runOnUiThread(()->fail(e));}
        },"Lite2-release-check").start();
    }
    static int compare(String left,String right){
        String[] a=left.split("\\."),b=right.split("\\.");
        for(int i=0;i<3;i++){
            int c=Integer.compare(Integer.parseInt(a[i]),Integer.parseInt(b[i]));
            if(c!=0)return c;
        }
        return 0;
    }
    private void startDownload(JSONObject asset,String version){
        try{
            String url=asset.getString("browser_download_url");
            if(!url.startsWith("https://github.com/"+REPO+"/releases/download/"))throw new Exception("GitHub URL");
            String name=asset.getString("name");
            File file=new File(getExternalFilesDir(null),name);
            if(file.exists() && !file.delete())throw new Exception(name);
            DownloadManager.Request request=new DownloadManager.Request(Uri.parse(url))
                .setTitle(name).setMimeType("application/vnd.android.package-archive")
                .setDestinationInExternalFilesDir(this,null,name);
            long id=downloads.enqueue(request);
            pending.edit().putLong("id",id).putString("version",version)
                .putString("digest",asset.optString("digest","")).apply();
            poll();
        }catch(Exception e){fail(e);}
    }
    private void poll(){
        if(!live() || waitingForPermission || installing || verifying)return;
        long id=pending.getLong("id",-1);
        try(Cursor cursor=downloads.query(new DownloadManager.Query().setFilterById(id))){
            if(cursor==null || !cursor.moveToFirst())throw new Exception("DownloadManager");
            int code=cursor.getInt(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_STATUS));
            if(code==DownloadManager.STATUS_FAILED)
                throw new Exception("DownloadManager "+cursor.getInt(cursor.getColumnIndexOrThrow(DownloadManager.COLUMN_REASON)));
            if(code!=DownloadManager.STATUS_SUCCESSFUL){
                status.setText(t("다운로드 중…"));
                main.postDelayed(this::poll,500);
                return;
            }
            verifying=true;
            new Thread(()->{
                try{
                    verify();
                    runOnUiThread(()->{if(live())install(id);});
                }catch(Exception e){runOnUiThread(()->fail(e));}
            },"Lite2-apk-check").start();
        }catch(Exception e){fail(e);}
    }
    private void verify() throws Exception{
        String version=pending.getString("version","");
        File file=new File(getExternalFilesDir(null),"Lite2Control-"+version+".apk");
        String expected=pending.getString("digest","");
        if(expected.startsWith("sha256:")){
            MessageDigest hash=MessageDigest.getInstance("SHA-256");
            try(InputStream in=new FileInputStream(file)){
                byte[] buffer=new byte[65536];
                int n;
                while((n=in.read(buffer))!=-1)hash.update(buffer,0,n);
            }
            StringBuilder hex=new StringBuilder("sha256:");
            for(byte b:hash.digest())hex.append(String.format(Locale.ROOT,"%02x",b&255));
            if(!expected.equalsIgnoreCase(hex.toString()))throw new Exception("SHA-256");
        }
        PackageManager pm=getPackageManager();
        PackageInfo apk=pm.getPackageArchiveInfo(file.getPath(),PackageManager.GET_SIGNATURES);
        PackageInfo current=pm.getPackageInfo(getPackageName(),PackageManager.GET_SIGNATURES);
        if(apk==null || !getPackageName().equals(apk.packageName) || !version.equals(apk.versionName)
            || apk.versionCode<=current.versionCode || current.signatures==null || current.signatures.length==0
            || !Arrays.equals(apk.signatures,current.signatures))throw new Exception("APK version / signature");
    }
    private void install(long id){
        try{
            if(!getPackageManager().canRequestPackageInstalls()){
                waitingForPermission=true;
                startActivityForResult(new Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES,Uri.parse("package:"+getPackageName())),1);
                return;
            }
            Uri apk=downloads.getUriForDownloadedFile(id);
            if(apk==null)throw new Exception("APK URI");
            installing=true;
            startActivityForResult(new Intent(Intent.ACTION_VIEW).setDataAndType(apk,"application/vnd.android.package-archive")
                .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION),2);
        }catch(Exception e){fail(e);}
    }
    @Override protected void onActivityResult(int request,int result,Intent data){
        super.onActivityResult(request,result,data);
        if(request==1){
            waitingForPermission=false;
            verifying=false;
            if(getPackageManager().canRequestPackageInstalls())poll();else finish();
        }else if(request==2){clearPending();finish();}
    }
    private void clearPending(){
        long id=pending.getLong("id",-1);
        if(id!=-1)downloads.remove(id);
        pending.edit().clear().apply();
    }
    private void fail(Exception e){
        if(!live())return;
        clearPending();
        status.setText(t("업데이트 실패"));
        new AlertDialog.Builder(this).setMessage(t("업데이트 실패")+"\n"+e.getLocalizedMessage())
            .setPositiveButton(android.R.string.ok,(d,w)->finish()).show();
    }
}
