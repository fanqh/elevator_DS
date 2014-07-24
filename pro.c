bool prodata(uint8 buff,uint16 size)
{
    uint16 i,next;
   for(i=0,j=0;i<size; i= next)
    {   
        if(buf[i]==0x68)
        {
            i++;
            next++;
        }
        else
            break;
        
        
        if(buf[i]==0xa)
        {
            i++;
            next++;
        }
        else
            break;
}