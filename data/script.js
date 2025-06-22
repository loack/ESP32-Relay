function onButton(id){
    var classlist = document.getElementById(id).classList
 //check if active or inactive
    if(classlist.contains("active"))
    {
        console.log("switching off");
        $.get("activate", id + "=off");
        classlist.toggle("active");
        console.log(document.getElementById(id).className)
    }
    else
    {
        console.log("switching on");
        $.get("activate", id + "=on");
        classlist.toggle("active");
        console.log(document.getElementById(id).className)
    }
}

function updateDisplay(data){
   console.log(data);
   //display heater status
   if(data["statusHeater1"] == 0 && data["statusHeater2"] == 0){
    document.getElementById("tempButton").classList.remove("active");
   }
   else{
    document.getElementById("tempButton").classList.add("active");
   }
   //display bubbles status
   if(data["statusBubbles"] == 0){
    document.getElementById("bubblesButton").classList.remove("active");
   }
   else{
    document.getElementById("bubblesButton").classList.add("active");
   }
   //display flow transfo status
   if(data["statusTransfo"] == 0){
    document.getElementById("flowButton").classList.remove("active");
   }
   else{
    document.getElementById("flowButton").classList.add("active");
   }
   //sensors
   document.getElementById("valeurTemperature").innerText = (data["valueTemp1"]+data["valueTemp2"])/2;
   document.getElementById("T1").innerText = data["valueTemp1"];
   document.getElementById("T2").innerText = data["valueTemp2"];
   document.getElementById("Flow").innerText = data["valueFlow"];
}

function getData()
{   
    var xhttp = new XMLHttpRequest();
    //definition du callback quand c'est prêt
    xhttp.onreadystatechange = function () {
        if(this.readyState == 4 && this.status == 200)
        {
            data = this.response;
            updateDisplay(JSON.parse(data));
            //document.getElementById("valeurTemperature").innerHTML = this.responseText; 
            //document.getElementById("valeurTemperature").innerHTML = this.responseText; 
        }
    }
    xhttp.open("GET","getData",true);
    xhttp.send();
}
// Actualisation des valeurs sur la page
setInterval(getData,3000); //requete toutes les 2000 ms