void le_pids(){
        bool res;

        
if (modo==3){  

           // ----------------------------------------------------------------
           // Para aumentar a velocidade do painel principal (modo=1),
           // não pergunta por pids que não estejam no display principal
           // O PID abaixo só é chamado no modo=3 (modo_lista_pids)
           // ----------------------------------------------------------------

          // ----- PIDS DISPONIVEIS ---
          if (sup_block ==0 ){
          res = obd.getCurrentPID(offset, 4);
          if (res){
            sup_block = obd.readUint32();
            // Debug básico
            //Serial.print("PID 0x");
            //Serial.print(offset, HEX);
            //Serial.print("  -> 0x");
            //Serial.println(sup_block, HEX);
            delay(pid_intervalo);            
          }
          }
          botao();  
}          
          
           // ----------------- RPM ----------------
            res = obd.getCurrentPID(0x0C, 2); 
            if (res){
                rpm=obd.readUint16()/4;
                falhas=0;
                delay(pid_intervalo);            
            }else{
                falhas++;
            }
            botao();  

           // ----------------- TPS ----------------            
            res = obd.getCurrentPID(0x11, 1); 
            if (res){ 
                tps = (obd.readUint8()*100)/255;
                falhas=0;
                delay(pid_intervalo);            
            }else{
                falhas++;
            }
            botao();  

           // ----------------- MAP ----------------                                  
            res = obd.getCurrentPID(0x0B, 1); 
            if (res){
                mapkpa=obd.readUint8();
                falhas=0;          
                delay(pid_intervalo);
            }else{
                falhas++;            
            }
            botao();  

           // ----------------- ECT ----------------                                  
            res = obd.getCurrentPID(0x05, 1); 
            if (res){
                temp=obd.readUint8()-40;
                falhas=0;          
                delay(pid_intervalo);
            }else{
                falhas++;            
            }
            botao();  



if (modo!=1){
           // -------------------------------------------------------------------------
           // Para aumentar a velocidade do painel principal (modo=1)
           // Suprimi as seguintes requisições que não pertencem ao display principal.
           // Elas são requisitadas apenas no modo=2 (scanner).
           // -------------------------------------------------------------------------
                      
           // ----------------- IAT ----------------                                  
            res = obd.getCurrentPID(0x0F, 1); 
            if (res){
                temp_ar=obd.readUint8()-40;
                falhas=0;          
                delay(pid_intervalo);
            }else{
                falhas++;            
            }
            botao();  

           // ----------------- CARGA ----------------                                  
            res = obd.getCurrentPID(0x04, 1); 
            if (res){
                carga = (obd.readUint8()*100)/255;
                falhas=0;          
                delay(pid_intervalo);
            }else{
                falhas++;            
            }
            botao();  


           // ----------------- AVANÇO--------------            
            res = obd.getCurrentPID(0x0E, 1); 
            if (res){
                adv=(obd.readUint8()/2.0)-64.0;
                //adv=12.5;
                falhas=0;          
                delay(pid_intervalo);
            }else{
                falhas++;            
            }
            botao();  


            

           // ----------------- SONDA 1 -------------                                  
            res = obd.getCurrentPID(0x14, 2); 
            if (res){
                sonda_1a=obd.readUint8();
                sonda_1b=obd.readUint8();
                tensao_sonda_1 = sonda_1a *5;
                fueltrim_sonda_1 = (sonda_1b - 128) * 100.0 / 128.0;
                falhas=0;          
                delay(pid_intervalo);
            }else{
                falhas++;            
            }
            botao();  

           // ----------------- SONDA 2 -------------                                  
            res = obd.getCurrentPID(0x15, 2); 
            if (res){
                sonda_2a=obd.readUint8();
                sonda_2b=obd.readUint8();
                tensao_sonda_2 = sonda_2a *5 ;
                fueltrim_sonda_2 = (sonda_2b - 128) * 100.0 / 128.0;
                falhas=0;          
                delay(pid_intervalo);
            }else{
                falhas++;            
            }
            botao();  

            // 0xD: 1  < Velocidade
            
            
}// fecha if (modo!1)


            
            if (falhas==0){
                digitalWrite(led_yellow_falhas, LOW);
            }    


}
